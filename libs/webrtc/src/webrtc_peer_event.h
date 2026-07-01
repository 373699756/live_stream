#ifndef LIVE_STREAM_WEBRTC_SRC_WEBRTC_PEER_EVENT_H_
#define LIVE_STREAM_WEBRTC_SRC_WEBRTC_PEER_EVENT_H_

#include "event.h"
#include "webrtc.h"
#include "webrtc_peer_table.h"

#include <mutex>
#include <string>

namespace live_stream {

class WebrtcPeerEvent {
public:
    WebrtcPeerEvent(event::EventCenter *event_center,
                    std::mutex *mutex,
                    webrtc_internal::WebrtcPeerTable *peer_table);

    void Publish(const WebrtcPeerInfo &peer,
                 WebrtcPeerState next_state,
                 const std::string &msg);

private:
    event::EventCenter *event_center_ = nullptr;
    std::mutex *mutex_ = nullptr;
    webrtc_internal::WebrtcPeerTable *peer_table_ = nullptr;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_WEBRTC_PEER_EVENT_H_
