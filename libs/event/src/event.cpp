#include "event.h"

#include "infra/executor.h"
#include "infra/time.h"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace live_stream {

namespace {

constexpr uint32_t kDefaultQueueCapacity = 1024;
constexpr uint32_t kDefaultMaxSubscriptions = 128;
constexpr uint32_t kMaxSourceLength = 64;
constexpr uint32_t kMaxTargetLength = 128;
constexpr uint32_t kMaxMessageLength = 256;

struct Subscription {
    std::vector<EventType> types;
    EventHandler handler;
};

EventOptions NormalizeOptions(EventOptions options) {
    if (options.queue_capacity == 0) {
        options.queue_capacity = kDefaultQueueCapacity;
    }
    if (options.max_subscriptions == 0) {
        options.max_subscriptions = kDefaultMaxSubscriptions;
    }
    return options;
}

bool IsKnownEventType(EventType type) {
    switch (type) {
        case EventType::kConfigChanged:
        case EventType::kMediaPipelineStarted:
        case EventType::kMediaPipelineStopped:
        case EventType::kMediaPipelineError:
        case EventType::kMediaStatusChanged:
        case EventType::kStreamStarted:
        case EventType::kStreamStopped:
        case EventType::kRtspClientConnected:
        case EventType::kRtspClientDisconnected:
        case EventType::kWebRtcClientConnected:
        case EventType::kWebRtcClientDisconnected:
        case EventType::kOnvifRequestReceived:
        case EventType::kSnapshotCreated:
        case EventType::kTimeChanged:
        case EventType::kNetworkChanged:
        case EventType::kAlarmOn:
        case EventType::kAlarmOff:
        case EventType::kSystemStatusChanged:
        case EventType::kUpgradeProgressChanged:
            return true;
    }
    return false;
}

bool IsEventSizeValid(const Event &event) {
    return event.source.size() <= kMaxSourceLength &&
           event.target.size() <= kMaxTargetLength &&
           event.message.size() <= kMaxMessageLength;
}

bool HasEventType(const std::vector<EventType> &types, EventType type) {
    for (EventType candidate : types) {
        if (candidate == type) {
            return true;
        }
    }
    return false;
}

bool AreEventTypesValid(const std::vector<EventType> &types) {
    if (types.empty()) {
        return false;
    }
    for (EventType type : types) {
        if (!IsKnownEventType(type)) {
            return false;
        }
    }
    return true;
}

class EventImpl : public IEvent {
public:
    explicit EventImpl(const EventOptions &options)
        : options_(NormalizeOptions(options)) {}

    ~EventImpl() override {
        ReleaseInternal();
    }

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return true;
        }
        if (!executor_) {
            executor_.reset(new infra::Executor());
        }
        initialized_ = executor_ != nullptr;
        return initialized_;
    }

    bool Start() override {
        if (!Prepare()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_) {
            return true;
        }
        infra::ExecutorOptions config;
        config.worker_count = 1;
        config.queue_capacity = options_.queue_capacity;
        if (!executor_->Start(config)) {
            return false;
        }
        started_ = true;
        return true;
    }

    void Stop() override {
        StopInternal();
    }

    void Release() {
        ReleaseInternal();
    }

private:
    void StopInternal() {
        std::unique_ptr<infra::Executor> executor;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = false;
            executor_.swap(executor);
        }
        if (executor) {
            executor->Stop(infra::StopMode::kDiscard);
            std::lock_guard<std::mutex> lock(mutex_);
            counts_.queued = 0;
            if (!executor_) {
                executor_.swap(executor);
            }
        }
    }

    void ReleaseInternal() {
        StopInternal();
        std::lock_guard<std::mutex> lock(mutex_);
        subscriptions_.clear();
        executor_.reset();
        initialized_ = false;
        started_ = false;
    }

public:
    EventSubscriptionId Subscribe(
        const std::vector<EventType> &types, EventHandler handler) override {
        if (!handler || !AreEventTypesValid(types)) {
            return 0;
        }
        if (!IsInitializedForRead()) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (subscriptions_.size() >= options_.max_subscriptions) {
            return 0;
        }
        const EventSubscriptionId subscription_id = next_subscription_id_++;
        subscriptions_[subscription_id] = Subscription{types, std::move(handler)};
        return subscription_id;
    }

    bool Unsubscribe(EventSubscriptionId subscription_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto erased = subscriptions_.erase(subscription_id);
        return erased != 0;
    }

    bool Publish(const Event &event) override {
        if (!IsKnownEventType(event.type) || !IsEventSizeValid(event)) {
            IncrementRejected();
            return false;
        }
        if (!IsStartedForRead()) {
            IncrementRejected();
            return false;
        }

        Event event_to_publish = event;
        if (event_to_publish.timestamp_ms == 0) {
            event_to_publish.timestamp_ms = infra::Time::SystemTimeMillis();
        }

        std::vector<EventSubscriptionId> subscription_ids;
        infra::Executor *executor = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!executor_) {
                ++counts_.rejected;
                return false;
            }
            executor = executor_.get();
            for (const auto &entry : subscriptions_) {
                if (HasEventType(entry.second.types, event_to_publish.type)) {
                    subscription_ids.push_back(entry.first);
                }
            }
        }
        if (subscription_ids.empty()) {
            IncrementPublished();
            return true;
        }

        IncrementQueued();
        if (!executor->Post([this, event_to_publish, subscription_ids]() {
                uint64_t handled_count = 0;
                for (EventSubscriptionId subscription_id : subscription_ids) {
                    EventHandler handler;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        const auto entry = subscriptions_.find(subscription_id);
                        if (entry == subscriptions_.end() ||
                            !HasEventType(entry->second.types,
                                          event_to_publish.type)) {
                            continue;
                        }
                        handler = entry->second.handler;
                    }
                    handler(event_to_publish);
                    ++handled_count;
                }
                FinishQueuedEvent(handled_count);
            })) {
            DropQueuedEvent();
            return false;
        }
        IncrementPublished();
        return true;
    }

    EventCounts GetCounts() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return counts_;
    }

private:
    bool IsInitializedForRead() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_;
    }

    bool IsStartedForRead() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
    }

    void IncrementRejected() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++counts_.rejected;
    }

    void IncrementPublished() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++counts_.published;
    }

    void IncrementQueued() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++counts_.queued;
    }

    void DropQueuedEvent() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (counts_.queued > 0) {
            --counts_.queued;
        }
        ++counts_.dropped;
        ++counts_.rejected;
    }

    void FinishQueuedEvent(uint64_t handled_count) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (counts_.queued > 0) {
            --counts_.queued;
        }
        counts_.handled += handled_count;
    }

    EventOptions options_;
    mutable std::mutex mutex_;
    std::unique_ptr<infra::Executor> executor_;
    std::unordered_map<EventSubscriptionId, Subscription> subscriptions_;
    EventCounts counts_;
    EventSubscriptionId next_subscription_id_ = 1;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<IEvent> CreateEvent(const EventOptions &options) {
    return std::unique_ptr<IEvent>(new EventImpl(options));
}

}  // namespace live_stream
