#include "webrtc.h"

#include "fake_media_streams.h"

#include <memory>

int main() {
    live_stream::test::FakeMediaStreams media_streams;

    live_stream::WebrtcOptions disabled_options;
    disabled_options.enabled = false;

    std::unique_ptr<live_stream::IWebrtc> disabled =
        live_stream::CreateWebrtc(disabled_options, nullptr);
    if (!disabled || !disabled->Start()) {
        return 1;
    }
    if (disabled->GetStats().enabled ||
        disabled->GetStats().signaling_ready ||
        !disabled->CreatePeer(live_stream::WebrtcCreatePeerRequest{})
             .peer_id.empty()) {
        return 2;
    }
    disabled->Stop();

    live_stream::WebrtcOptions options;
    options.max_peers = 1;
    std::unique_ptr<live_stream::IWebrtc> service =
        live_stream::CreateWebrtc(options, nullptr);
    if (!service || !service->Start()) {
        return 3;
    }
    live_stream::WebrtcCreatePeerRequest create_request;
    create_request.stream_id = live_stream::StreamId::kMain;
    if (!service->CreatePeer(create_request).peer_id.empty()) {
        return 4;
    }
    if (!service->GetPeer("missing-peer").peer_id.empty() ||
        service->ClosePeer("missing-peer")) {
        return 5;
    }
    service->Stop();
    return 0;
}
