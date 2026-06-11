#ifndef LIVE_STREAM_EVENT_EVENT_H_
#define LIVE_STREAM_EVENT_EVENT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

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

// Event carries lightweight status/control metadata only. Do not put media
// frames, binary payloads, credentials, or large JSON in event fields.
struct Event {
    EventType type = EventType::kConfigChanged;
    std::string source;
    std::string target;
    std::string message;
    int32_t value = 0;
    int64_t timestamp_ms = 0;
    uint8_t level = 0;
};

using EventSubscriptionId = uint64_t;

struct EventOptions {
    uint32_t queue_capacity = 1024;
    uint32_t max_subscriptions = 128;
};

struct EventCounts {
    uint64_t published = 0;
    uint64_t handled = 0;
    uint64_t dropped = 0;
    uint64_t rejected = 0;
    uint32_t queued = 0;
};

// Handlers must be lightweight. If business work is needed, post it to the
// subscriber service's own TaskQueue instead of blocking event-thread.
using EventHandler = std::function<void(const Event&)>;

class IEvent {
public:
    virtual ~IEvent() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    // The handler is invoked asynchronously on event-thread.
    virtual EventSubscriptionId Subscribe(
        const std::vector<EventType> &types, EventHandler handler) = 0;
    virtual bool Unsubscribe(EventSubscriptionId subscription_id) = 0;
    virtual bool Publish(const Event& event) = 0;
    virtual EventCounts GetCounts() const = 0;
};

std::unique_ptr<IEvent> CreateEvent(
    const EventOptions &options = EventOptions());

}  // namespace live_stream

#endif  // LIVE_STREAM_EVENT_EVENT_H_
