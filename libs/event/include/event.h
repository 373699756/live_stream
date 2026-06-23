#ifndef LIVE_STREAM_EVENT_EVENT_H_
#define LIVE_STREAM_EVENT_EVENT_H_

#include "loop.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {
namespace event {

using SubscriptionId = uint64_t;

enum class EventType {
    kConfigChanged,
    kMediaPipelineStarted,
    kMediaPipelineStopped,
    kMediaPipelineError,
    kMediaStatusChanged,
    kStreamStarted,
    kStreamStopped,
    kRtspClientConnected,
    kRtspClientDisconnected,
    kWebRtcClientConnected,
    kWebRtcClientDisconnected,
    kOnvifRequestReceived,
    kSnapshotCreated,
    kTimeChanged,
    kNetworkChanged,
    kAlarmOn,
    kAlarmOff,
    kSystemStatusChanged,
    kUpgradeProgressChanged,
};

// Event is for lightweight state/control metadata. Media frames, credentials,
// binary payloads and large JSON stay in the module that owns that data.
struct Event {
    EventType type = EventType::kConfigChanged;
    std::string source;
    std::string target;
    std::string message;
    int32_t value = 0;
    int64_t timestamp_ms = 0;
    uint8_t level = 0;
};

struct EventCounts {
    uint64_t published = 0;
    uint64_t handled = 0;
    uint64_t rejected = 0;
    uint32_t subscriptions = 0;
};

using EventFn = std::function<void(const Event &)>;

class Dispatcher;

// RAII handle returned by Dispatcher subscriptions. Destroying or cancelling it
// detaches the handler from future Publish() calls.
class Subscription {
public:
    Subscription() = default;
    ~Subscription();

    Subscription(Subscription &&other) noexcept;
    Subscription &operator=(Subscription &&other) noexcept;

    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;

    bool valid() const { return dispatcher_ != nullptr && id_ != 0; }
    void Cancel();

private:
    friend class Dispatcher;

    Subscription(Dispatcher *dispatcher, SubscriptionId id)
        : dispatcher_(dispatcher), id_(id) {}

    Dispatcher *dispatcher_ = nullptr;
    SubscriptionId id_ = 0;
};

// Synchronous in-process event fanout. Handlers run in the Publish() caller, so
// slow work should be posted to the owning module's queue.
class Dispatcher {
public:
    Dispatcher();
    ~Dispatcher();

    Dispatcher(const Dispatcher &) = delete;
    Dispatcher &operator=(const Dispatcher &) = delete;

    Subscription Subscribe(EventType type, EventFn fn);
    // Subscribes one handler to any event whose type is listed in types.
    Subscription SubscribeTypes(const std::vector<EventType> &types,
                                EventFn fn);
    bool Cancel(SubscriptionId id);
    EventStatus Publish(const Event &event);
    EventStatus Post(Loop *loop, const Event &event);
    EventCounts GetCounts() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

struct ServiceOptions {
    LoopOptions loop;
};

// Owns the event loop plus dispatcher used for asynchronous event publishing.
class Service {
public:
    Service();
    ~Service();

    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;

    bool Start(const ServiceOptions &options = ServiceOptions());
    void Stop(StopMode mode = StopMode::kDiscard);
    Loop *loop() { return &loop_; }
    Dispatcher *dispatcher() { return &dispatcher_; }
    EventStatus Publish(const Event &event);
    EventStatus PublishAsync(const Event &event);

private:
    Loop loop_;
    Dispatcher dispatcher_;
    bool started_ = false;
};

bool IsKnownEventType(EventType type);

}  // namespace event
}  // namespace live_stream

#endif  // LIVE_STREAM_EVENT_EVENT_H_
