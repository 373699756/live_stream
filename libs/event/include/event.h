#ifndef LIVE_STREAM_EVENT_EVENT_H_
#define LIVE_STREAM_EVENT_EVENT_H_

#include "loop.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace live_stream {
namespace event {

using EventTokenId = uint64_t;

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
    kNetQueueChanged,
    kAlarmOn,
    kAlarmOff,
    kSystemInfoChanged,
    kUpgradeProgressChanged,
};

// Event is for lightweight state/control metadata. Media frames, credentials,
// binary payloads and large JSON stay in the module that owns that data.
struct Event {
    EventType type = EventType::kConfigChanged;
    std::string source;
    std::string target;
    std::string msg;
    int32_t value = 0;
    int64_t timestamp_ms = 0;
    uint8_t level = 0;
};

struct EventStats {
    uint64_t published = 0;
    uint64_t handled = 0;
    uint64_t rejected = 0;
    uint32_t subscriptions = 0;
};

using EventHandler = std::function<void(const Event &)>;

class EventCenter;

// RAII handle returned by EventCenter subscriptions. Destroying or cancelling it
// detaches the handler from future Publish() calls.
class EventToken {
public:
    EventToken() = default;
    ~EventToken();

    EventToken(EventToken &&other) noexcept;
    EventToken &operator=(EventToken &&other) noexcept;

    EventToken(const EventToken &) = delete;
    EventToken &operator=(const EventToken &) = delete;

    bool valid() const { return center_ != nullptr && id_ != 0; }
    void Cancel();

private:
    friend class EventCenter;

    EventToken(EventCenter *center, EventTokenId id)
        : center_(center), id_(id) {}

    EventCenter *center_ = nullptr;
    EventTokenId id_ = 0;
};

// Synchronous in-process event fanout. Handlers run in the Publish() caller, so
// slow work should be posted to the owning module's queue. Listeners are stored
// by EventType, so Publish() only snapshots the matching bucket.
class EventCenter {
public:
    EventCenter();
    ~EventCenter();

    EventCenter(const EventCenter &) = delete;
    EventCenter &operator=(const EventCenter &) = delete;

    EventToken Subscribe(EventType type, void *tag, EventHandler handler);
    bool Unsubscribe(EventType type, void *tag);
    void UnsubscribeTag(void *tag);
    bool Cancel(EventTokenId id);
    EventStatus Publish(const Event &event);
    EventStatus Post(Loop *loop, const Event &event);
    EventStats GetStats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

struct BusOptions {
    LoopOptions loop;
};

// Owns the event loop plus event center used for asynchronous publishing.
class EventBus {
public:
    EventBus();
    ~EventBus();

    EventBus(const EventBus &) = delete;
    EventBus &operator=(const EventBus &) = delete;

    bool Start(const BusOptions &options = BusOptions());
    void Stop(StopMode mode = StopMode::kDiscard);
    Loop *loop() { return &loop_; }
    EventCenter *center() { return &center_; }
    EventStatus Publish(const Event &event);
    EventStatus PublishAsync(const Event &event);

private:
    Loop loop_;
    EventCenter center_;
    bool started_ = false;
};

bool IsKnownEventType(EventType type);

}  // namespace event
}  // namespace live_stream

#endif  // LIVE_STREAM_EVENT_EVENT_H_
