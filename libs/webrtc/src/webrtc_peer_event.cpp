#include "webrtc_peer_event.h"

#include <cstdint>

namespace live_stream {

WebrtcPeerEvent::WebrtcPeerEvent(
    event::EventCenter *event_center,
    std::mutex *mutex,
    webrtc_internal::WebrtcPeerTable *peer_table)
    : event_center_(event_center),
      mutex_(mutex),
      peer_table_(peer_table) {}

void WebrtcPeerEvent::Publish(const WebrtcPeerInfo &peer,
                              WebrtcPeerState next_state,
                              const std::string &msg) {
    if (event_center_ == nullptr || peer.peer_id.empty()) {
        return;
    }
    event::Event webrtc_event;
    webrtc_event.source = Webrtc::Name();
    webrtc_event.target = peer.peer_id;
    webrtc_event.msg = msg;
    {
        std::lock_guard<std::mutex> guard(*mutex_);
        webrtc_event.value =
            static_cast<int32_t>(peer_table_->ActivePeers());
    }
    if (next_state == WebrtcPeerState::kConnected &&
        peer.state != WebrtcPeerState::kConnected) {
        webrtc_event.type = event::EventType::kWebRtcClientConnected;
        static_cast<void>(event_center_->Publish(webrtc_event));
        return;
    }
    if ((next_state == WebrtcPeerState::kClosing ||
         next_state == WebrtcPeerState::kClosed ||
         next_state == WebrtcPeerState::kFailed) &&
        peer.state == WebrtcPeerState::kConnected) {
        webrtc_event.type = event::EventType::kWebRtcClientDisconnected;
        static_cast<void>(event_center_->Publish(webrtc_event));
    }
}

}  // namespace live_stream
