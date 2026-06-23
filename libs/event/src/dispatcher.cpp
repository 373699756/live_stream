#include "event.h"

#include "infra/time.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace live_stream {
namespace event {
namespace {

constexpr uint32_t kDefaultMaxSubscriptions = 256;
constexpr uint32_t kMaxSourceLength = 64;
constexpr uint32_t kMaxTargetLength = 128;
constexpr uint32_t kMaxMessageLength = 256;

bool IsEventSizeValid(const Event &event) {
    return event.source.size() <= kMaxSourceLength &&
           event.target.size() <= kMaxTargetLength &&
           event.message.size() <= kMaxMessageLength;
}

bool ContainsEventType(const std::vector<EventType> &types, EventType type) {
    for (EventType candidate : types) {
        if (candidate == type) {
            return true;
        }
    }
    return false;
}

bool IsEventTypesValid(const std::vector<EventType> &types) {
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

Event WithTimestamp(Event event) {
    if (event.timestamp_ms == 0) {
        event.timestamp_ms = infra::Time::SystemTimeMillis();
    }
    return event;
}

}  // namespace

class Dispatcher::Impl {
public:
    Subscription SubscribeTypes(Dispatcher *owner,
                                const std::vector<EventType> &types,
                                EventFn fn) {
        if (!owner || !fn || !IsEventTypesValid(types)) {
            return Subscription();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (subscriptions_.size() >= kDefaultMaxSubscriptions) {
            ++counts_.rejected;
            return Subscription();
        }
        const SubscriptionId id = next_subscription_id_++;
        subscriptions_[id] = Entry{types, std::move(fn)};
        return Subscription(owner, id);
    }

    bool Cancel(SubscriptionId id) {
        if (id == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return subscriptions_.erase(id) != 0;
    }

    EventStatus Publish(const Event &event) {
        if (!IsKnownEventType(event.type) || !IsEventSizeValid(event)) {
            IncrementRejected();
            return EventStatus::kInvalid;
        }
        const Event event_to_publish = WithTimestamp(event);
        std::vector<EventFn> fns;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto &entry : subscriptions_) {
                if (ContainsEventType(entry.second.types,
                                      event_to_publish.type)) {
                    fns.push_back(entry.second.fn);
                }
            }
            ++counts_.published;
        }
        for (const EventFn &fn : fns) {
            if (fn) {
                fn(event_to_publish);
                std::lock_guard<std::mutex> lock(mutex_);
                ++counts_.handled;
            }
        }
        return EventStatus::kOk;
    }

    EventStatus Post(Dispatcher *owner, Loop *loop, const Event &event) {
        if (owner == nullptr || loop == nullptr) {
            IncrementRejected();
            return EventStatus::kInvalid;
        }
        Event event_to_publish = WithTimestamp(event);
        return loop->Post([owner, event_to_publish]() {
            (void)owner->Publish(event_to_publish);
        });
    }

    EventCounts GetCounts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        EventCounts counts = counts_;
        counts.subscriptions = static_cast<uint32_t>(subscriptions_.size());
        return counts;
    }

private:
    struct Entry {
        std::vector<EventType> types;
        EventFn fn;
    };

    void IncrementRejected() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++counts_.rejected;
    }

    mutable std::mutex mutex_;
    std::unordered_map<SubscriptionId, Entry> subscriptions_;
    EventCounts counts_;
    SubscriptionId next_subscription_id_ = 1;
};

Dispatcher::Dispatcher() : impl_(new Impl()) {}
Dispatcher::~Dispatcher() = default;

Subscription Dispatcher::Subscribe(EventType type, EventFn fn) {
    return impl_->SubscribeTypes(this, std::vector<EventType>{type},
                                 std::move(fn));
}

Subscription Dispatcher::SubscribeTypes(const std::vector<EventType> &types,
                                        EventFn fn) {
    return impl_->SubscribeTypes(this, types, std::move(fn));
}

bool Dispatcher::Cancel(SubscriptionId id) { return impl_->Cancel(id); }

EventStatus Dispatcher::Publish(const Event &event) {
    return impl_->Publish(event);
}

EventStatus Dispatcher::Post(Loop *loop, const Event &event) {
    return impl_->Post(this, loop, event);
}

EventCounts Dispatcher::GetCounts() const { return impl_->GetCounts(); }

Subscription::~Subscription() { Cancel(); }

Subscription::Subscription(Subscription &&other) noexcept
    : dispatcher_(other.dispatcher_), id_(other.id_) {
    other.dispatcher_ = nullptr;
    other.id_ = 0;
}

Subscription &Subscription::operator=(Subscription &&other) noexcept {
    if (this != &other) {
        Cancel();
        dispatcher_ = other.dispatcher_;
        id_ = other.id_;
        other.dispatcher_ = nullptr;
        other.id_ = 0;
    }
    return *this;
}

void Subscription::Cancel() {
    Dispatcher *dispatcher = dispatcher_;
    const SubscriptionId id = id_;
    dispatcher_ = nullptr;
    id_ = 0;
    if (dispatcher != nullptr && id != 0) {
        (void)dispatcher->Cancel(id);
    }
}

Service::Service() = default;
Service::~Service() { Stop(StopMode::kDiscard); }

bool Service::Start(const ServiceOptions &options) {
    if (started_) {
        return true;
    }
    if (!loop_.Start(options.loop)) {
        return false;
    }
    started_ = true;
    return true;
}

void Service::Stop(StopMode mode) {
    loop_.Stop(mode);
    started_ = false;
}

EventStatus Service::Publish(const Event &event) {
    return dispatcher_.Publish(event);
}

EventStatus Service::PublishAsync(const Event &event) {
    return dispatcher_.Post(&loop_, event);
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

}  // namespace event
}  // namespace live_stream
