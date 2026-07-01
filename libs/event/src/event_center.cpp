#include "event.h"

#include "infra/time.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace live_stream {
namespace event {
namespace {

constexpr uint32_t kDefaultMaxSubscriptions = 256;
constexpr uint32_t kMaxSourceLength = 64;
constexpr uint32_t kMaxTargetLength = 128;
constexpr uint32_t kMaxMsgLength = 256;

bool IsEventSizeValid(const Event &event) {
    return event.source.size() <= kMaxSourceLength &&
           event.target.size() <= kMaxTargetLength &&
           event.msg.size() <= kMaxMsgLength;
}

Event WithTimestamp(Event event) {
    if (event.timestamp_ms == 0) {
        event.timestamp_ms = infra::Time::SystemTimeMillis();
    }
    return event;
}

}  // namespace

class EventCenter::Impl {
public:
    EventToken Subscribe(EventCenter *owner,
                         EventType type,
                         void *tag,
                         EventHandler handler) {
        if (owner == nullptr || !handler || !IsKnownEventType(type)) {
            return EventToken();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (token_types_.size() >= kDefaultMaxSubscriptions) {
            ++stats_.rejected;
            return EventToken();
        }
        const EventTokenId id = next_token_id_++;
        listeners_[type].push_back(Listener{id, tag, std::move(handler)});
        token_types_[id] = type;
        return EventToken(owner, id);
    }

    bool Unsubscribe(EventType type, void *tag) {
        if (!IsKnownEventType(type)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto bucket_iter = listeners_.find(type);
        if (bucket_iter == listeners_.end()) {
            return false;
        }
        bool erased = false;
        auto &bucket = bucket_iter->second;
        for (auto iter = bucket.begin(); iter != bucket.end();) {
            if (iter->tag == tag) {
                token_types_.erase(iter->id);
                iter = bucket.erase(iter);
                erased = true;
            } else {
                ++iter;
            }
        }
        if (bucket.empty()) {
            listeners_.erase(bucket_iter);
        }
        return erased;
    }

    void UnsubscribeTag(void *tag) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto bucket_iter = listeners_.begin();
             bucket_iter != listeners_.end();) {
            auto &bucket = bucket_iter->second;
            for (auto iter = bucket.begin(); iter != bucket.end();) {
                if (iter->tag == tag) {
                    token_types_.erase(iter->id);
                    iter = bucket.erase(iter);
                } else {
                    ++iter;
                }
            }
            if (bucket.empty()) {
                bucket_iter = listeners_.erase(bucket_iter);
            } else {
                ++bucket_iter;
            }
        }
    }

    bool Cancel(EventTokenId id) {
        if (id == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto type_iter = token_types_.find(id);
        if (type_iter == token_types_.end()) {
            return false;
        }
        const EventType type = type_iter->second;
        token_types_.erase(type_iter);
        auto bucket_iter = listeners_.find(type);
        if (bucket_iter == listeners_.end()) {
            return false;
        }
        auto &bucket = bucket_iter->second;
        const auto old_size = bucket.size();
        bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                    [id](const Listener &listener) {
                                        return listener.id == id;
                                    }),
                     bucket.end());
        if (bucket.empty()) {
            listeners_.erase(bucket_iter);
        }
        return bucket.size() != old_size;
    }

    EventStatus Publish(const Event &event) {
        if (!IsKnownEventType(event.type) || !IsEventSizeValid(event)) {
            IncrementRejected();
            return EventStatus::kInvalid;
        }
        const Event event_to_publish = WithTimestamp(event);
        std::vector<EventHandler> handlers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto bucket_iter = listeners_.find(event_to_publish.type);
            if (bucket_iter != listeners_.end()) {
                handlers.reserve(bucket_iter->second.size());
                for (const Listener &listener : bucket_iter->second) {
                    handlers.push_back(listener.handler);
                }
            }
            ++stats_.published;
        }
        for (const EventHandler &handler : handlers) {
            if (handler) {
                handler(event_to_publish);
            }
        }
        if (!handlers.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_.handled += handlers.size();
        }
        return EventStatus::kOk;
    }

    EventStatus Post(EventCenter *owner, Loop *loop, const Event &event) {
        if (owner == nullptr || loop == nullptr) {
            IncrementRejected();
            return EventStatus::kInvalid;
        }
        Event event_to_publish = WithTimestamp(event);
        return loop->Post([owner, event_to_publish]() {
            (void)owner->Publish(event_to_publish);
        });
    }

    EventStats GetStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        EventStats stats = stats_;
        stats.subscriptions = static_cast<uint32_t>(token_types_.size());
        return stats;
    }

private:
    struct Listener {
        EventTokenId id = 0;
        void *tag = nullptr;
        EventHandler handler;
    };

    void IncrementRejected() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.rejected;
    }

    mutable std::mutex mutex_;
    std::map<EventType, std::vector<Listener>> listeners_;
    std::unordered_map<EventTokenId, EventType> token_types_;
    EventStats stats_;
    EventTokenId next_token_id_ = 1;
};

EventCenter::EventCenter() : impl_(new Impl()) {}
EventCenter::~EventCenter() = default;

EventToken EventCenter::Subscribe(EventType type,
                                  void *tag,
                                  EventHandler handler) {
    return impl_->Subscribe(this, type, tag, std::move(handler));
}

bool EventCenter::Unsubscribe(EventType type, void *tag) {
    return impl_->Unsubscribe(type, tag);
}

void EventCenter::UnsubscribeTag(void *tag) {
    impl_->UnsubscribeTag(tag);
}

bool EventCenter::Cancel(EventTokenId id) { return impl_->Cancel(id); }

EventStatus EventCenter::Publish(const Event &event) {
    return impl_->Publish(event);
}

EventStatus EventCenter::Post(Loop *loop, const Event &event) {
    return impl_->Post(this, loop, event);
}

EventStats EventCenter::GetStats() const { return impl_->GetStats(); }

EventToken::~EventToken() { Cancel(); }

EventToken::EventToken(EventToken &&other) noexcept
    : center_(other.center_), id_(other.id_) {
    other.center_ = nullptr;
    other.id_ = 0;
}

EventToken &EventToken::operator=(EventToken &&other) noexcept {
    if (this != &other) {
        Cancel();
        center_ = other.center_;
        id_ = other.id_;
        other.center_ = nullptr;
        other.id_ = 0;
    }
    return *this;
}

void EventToken::Cancel() {
    EventCenter *center = center_;
    const EventTokenId id = id_;
    center_ = nullptr;
    id_ = 0;
    if (center != nullptr && id != 0) {
        (void)center->Cancel(id);
    }
}

EventBus::EventBus() = default;
EventBus::~EventBus() { Stop(StopMode::kDiscard); }

bool EventBus::Start(const BusOptions &options) {
    if (started_) {
        return true;
    }
    if (!loop_.Start(options.loop)) {
        return false;
    }
    started_ = true;
    return true;
}

void EventBus::Stop(StopMode mode) {
    loop_.Stop(mode);
    started_ = false;
}

EventStatus EventBus::Publish(const Event &event) {
    return center_.Publish(event);
}

EventStatus EventBus::PublishAsync(const Event &event) {
    return center_.Post(&loop_, event);
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
        case EventType::kNetQueueChanged:
        case EventType::kAlarmOn:
        case EventType::kAlarmOff:
        case EventType::kSystemInfoChanged:
        case EventType::kUpgradeProgressChanged:
            return true;
    }
    return false;
}

}  // namespace event
}  // namespace live_stream
