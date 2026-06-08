#include "webrtc.h"

#include "fake_media_source.h"

#include <cstring>
#include <memory>

namespace {

int Expect(bool condition) {
    return condition ? 0 : 1;
}

}  // namespace

int main() {
    if (std::strcmp(live_stream::Webrtc::Name(), "webrtc") != 0) {
        return 1;
    }

    live_stream::WebrtcOptions invalid_options;
    invalid_options.max_peers = 0;
    live_stream::WebrtcDependencies dependencies;
    std::unique_ptr<live_stream::IWebrtc> invalid_webrtc =
        live_stream::CreateWebrtc(invalid_options, dependencies);
    if (Expect(!invalid_webrtc->Start())) {
        return 1;
    }

    live_stream::WebrtcOptions options;
    options.max_peers = 4;
    options.session_timeout_ms = 30000;
    options.local_port_base = 16000;
    options.public_ip = "192.0.2.10";
    options.ice_servers.push_back(live_stream::WebrtcIceServer{
        "stun:stun.example.com:3478", "", ""});

    live_stream::test::FakeMediaFrameSource media_source;
    dependencies.media_source = &media_source;

    std::unique_ptr<live_stream::IWebrtc> service =
        live_stream::CreateWebrtc(options, dependencies);
    if (Expect(!service->Start())) {
        return 1;
    }

    live_stream::WebrtcStats initial_stats = service->GetStats();
    if (Expect(initial_stats.enabled) ||
        Expect(initial_stats.active_peers == 0) ||
        Expect(initial_stats.max_peers == 4)) {
        return 1;
    }

    live_stream::WebrtcCreatePeerRequest create_request;
    if (Expect(service->CreatePeer(create_request).peer_id.empty())) {
        return 1;
    }

    live_stream::WebrtcOfferRequest offer;
    offer.peer_id = "missing-peer";
    offer.sdp = "v=0\r\n";
    if (Expect(service->HandleOffer(offer).sdp.empty())) {
        return 1;
    }

    live_stream::WebrtcIceCandidate candidate;
    candidate.peer_id = "missing-peer";
    candidate.candidate = "candidate:1 1 UDP 1 10.0.0.2 10000 typ host";
    if (Expect(!service->AddIceCandidate(candidate))) {
        return 1;
    }

    service->Stop();
    if (Expect(service->CreatePeer(create_request).peer_id.empty())) {
        return 1;
    }
    return 0;
}
