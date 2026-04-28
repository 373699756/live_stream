#include "event_service.h"

#include "infra/executor.h"
#include "infra/service.h"

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

class EventServiceImpl : public IEventService, private infra::ServiceBase {
 public:
    EventServiceImpl() = default;
    ~EventServiceImpl() override {
        Stop();
        Deinit();
    }

    infra::Status Init() override { return infra::ServiceBase::Init(); }
    infra::Status Start() override { return infra::ServiceBase::Start(); }
    void Stop() override { infra::ServiceBase::Stop(); }
    void Deinit() override { infra::ServiceBase::Deinit(); }

    const char* Name() const override {
        return "event_service";
    }

    infra::Result<EventSubscriptionId> Subscribe(
        EventType type, EventHandler handler) override {
        if (!handler) {
            return infra::Result<EventSubscriptionId>::Fail(
                infra::Status::kInvalidParam);
        }
        if (!IsInitializedForRead()) {
            return infra::Result<EventSubscriptionId>::Fail(
                infra::Status::kBusy);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (subscriptions_.size() >= kMaxSubscriptions) {
            return infra::Result<EventSubscriptionId>::Fail(
                infra::Status::kBusy);
        }
        const EventSubscriptionId subscription_id = next_subscription_id_++;
        subscriptions_[subscription_id] = Subscription{type, std::move(handler)};
        return infra::Result<EventSubscriptionId>::Ok(subscription_id);
    }

    infra::Status Unsubscribe(EventSubscriptionId subscription_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto erased = subscriptions_.erase(subscription_id);
        return erased == 0 ? infra::Status::kNotFound : infra::Status::kOk;
    }

    infra::Status Publish(const Event& event) override {
        if (!IsEventSizeValid(event)) {
            return infra::Status::kInvalidParam;
        }
        if (!IsStartedForRead()) {
            return infra::Status::kBusy;
        }

        std::vector<EventHandler> handlers;
        infra::Executor* executor = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!executor_) {
                return infra::Status::kBusy;
            }
            executor = executor_.get();
            for (const auto& entry : subscriptions_) {
                if (entry.second.type == event.type) {
                    handlers.push_back(entry.second.handler);
                }
            }
        }
        if (handlers.empty()) {
            return infra::Status::kOk;
        }

        return executor->Post([event, handlers]() {
            for (const EventHandler& handler : handlers) {
                handler(event);
            }
        });
    }

 private:
    infra::Status OnInit() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (executor_) {
            return infra::Status::kOk;
        }

        executor_.reset(new infra::Executor());
        return infra::Status::kOk;
    }

    infra::Status OnStart() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!executor_) {
            return infra::Status::kInternalError;
        }
        infra::ExecutorOptions config;
        config.worker_count = 1;
        config.queue_capacity = 1024;
        return executor_->Start(config);
    }

    void OnStop() override {
        std::unique_ptr<infra::Executor> executor;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            executor = std::move(executor_);
        }
        if (executor) {
            executor->Stop(infra::StopMode::kDiscard);
            std::lock_guard<std::mutex> lock(mutex_);
            if (!executor_) {
                executor_ = std::move(executor);
            }
        }
    }

    void OnDeinit() override {
        std::lock_guard<std::mutex> lock(mutex_);
        subscriptions_.clear();
        executor_.reset();
    }

    mutable std::mutex mutex_;
    std::unique_ptr<infra::Executor> executor_;
    std::unordered_map<EventSubscriptionId, Subscription> subscriptions_;
    EventSubscriptionId next_subscription_id_ = 1;
};

}  // namespace

std::unique_ptr<IEventService> CreateEventService() {
    return std::unique_ptr<IEventService>(new EventServiceImpl());
}

}  // namespace live_stream
