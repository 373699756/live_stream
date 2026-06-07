#include "event.h"

#include "infra/executor.h"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace live_stream {

namespace {

constexpr uint32_t kMaxSubscriptions = 128;
constexpr uint32_t kMaxSourceLength = 64;
constexpr uint32_t kMaxTargetLength = 128;
constexpr uint32_t kMaxMessageLength = 256;

struct Subscription {
    EventType type = EventType::kConfigChanged;
    EventHandler handler;
};

bool IsEventSizeValid(const Event& event) {
    return event.source.size() <= kMaxSourceLength &&
           event.target.size() <= kMaxTargetLength &&
           event.message.size() <= kMaxMessageLength;
}

class EventServiceImpl : public IEvent {
public:
    EventServiceImpl() = default;
    ~EventServiceImpl() override {
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
        config.queue_capacity = 1024;
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
        EventType type, EventHandler handler) override {
        if (!handler) {
            return 0;
        }
        if (!IsInitializedForRead()) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (subscriptions_.size() >= kMaxSubscriptions) {
            return 0;
        }
        const EventSubscriptionId subscription_id = next_subscription_id_++;
        subscriptions_[subscription_id] = Subscription{type, std::move(handler)};
        return subscription_id;
    }

    bool Unsubscribe(EventSubscriptionId subscription_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto erased = subscriptions_.erase(subscription_id);
        return erased != 0;
    }

    bool Publish(const Event& event) override {
        if (!IsEventSizeValid(event)) {
            return false;
        }
        if (!IsStartedForRead()) {
            return false;
        }

        std::vector<EventHandler> handlers;
        infra::Executor* executor = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!executor_) {
                return false;
            }
            executor = executor_.get();
            for (const auto& entry : subscriptions_) {
                if (entry.second.type == event.type) {
                    handlers.push_back(entry.second.handler);
                }
            }
        }
        if (handlers.empty()) {
            return true;
        }

        return executor->Post([event, handlers]() {
            for (const EventHandler& handler : handlers) {
                handler(event);
            }
        });
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

    mutable std::mutex mutex_;
    std::unique_ptr<infra::Executor> executor_;
    std::unordered_map<EventSubscriptionId, Subscription> subscriptions_;
    EventSubscriptionId next_subscription_id_ = 1;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<IEvent> CreateEvent() {
    return std::unique_ptr<IEvent>(new EventServiceImpl());
}

}  // namespace live_stream
