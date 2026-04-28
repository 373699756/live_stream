#ifndef LIVE_STREAM_EVENT_SERVICE_H_
#define LIVE_STREAM_EVENT_SERVICE_H_

#include "infra/status.h"
#include "infra/service.h"

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
    kStorageStateChanged,
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

class IEventService : public infra::IService {
 public:
    // The handler is invoked asynchronously on event-thread.
    virtual infra::Result<EventSubscriptionId> Subscribe(
        EventType type, EventHandler handler) = 0;
    virtual infra::Status Unsubscribe(EventSubscriptionId subscription_id) = 0;
    virtual infra::Status Publish(const Event& event) = 0;
};

std::unique_ptr<IEventService> CreateEventService();

}  // namespace live_stream

#endif  // LIVE_STREAM_EVENT_SERVICE_H_
