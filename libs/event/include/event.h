#ifndef LIVE_STREAM_EVENT_EVENT_H_
#define LIVE_STREAM_EVENT_EVENT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

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
    kAlarmTriggered,
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
};

using EventSubscriptionId = uint64_t;

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
        EventType type, EventHandler handler) = 0;
    virtual bool Unsubscribe(EventSubscriptionId subscription_id) = 0;
    virtual bool Publish(const Event& event) = 0;
};

std::unique_ptr<IEvent> CreateEvent();

}  // namespace live_stream

#endif  // LIVE_STREAM_EVENT_EVENT_H_
