#include "webrtc_service.h"

#include <cstring>
#include <memory>

namespace {

int Expect(bool condition) {
    return condition ? 0 : 1;
}

}  // namespace

int main() {
    if (std::strcmp(live_stream::WebrtcService::Name(), "webrtc_service") != 0) {
        return 1;
    }

    live_stream::WebrtcServiceOptions invalid_options;
    invalid_options.max_peers = 0;
    live_stream::WebrtcServiceDependencies dependencies;
    std::unique_ptr<live_stream::IWebrtcService> invalid_service =
        live_stream::CreateWebrtcService(invalid_options, dependencies);
    if (Expect(!invalid_service->Start())) {
        return 1;
    }

    live_stream::WebrtcServiceOptions options;
    options.max_peers = 4;
    options.session_timeout_ms = 30000;
    options.local_port_base = 16000;
    options.public_ip = "192.0.2.10";
    options.ice_servers.push_back(live_stream::WebrtcIceServer{
        "stun:stun.example.com:3478", "", ""});

    std::unique_ptr<live_stream::IWebrtcService> service =
        live_stream::CreateWebrtcService(options, dependencies);
    if (Expect(!service->Start()) ||
        Expect(std::strcmp(service->BackendName(), "metaRTC") == 0)) {
        return 1;
    }

    live_stream::WebrtcServiceStats initial_stats = service->GetStats();
    if (Expect(initial_stats.enabled) ||
        Expect(!initial_stats.backend_available) ||
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

    live_stream::EncodedFrame frame;
    frame.stream_id = live_stream::StreamId::kMain;
    frame.buffer = live_stream::VideoBufferAlloc(8);
    frame.size = 4;
    (void)live_stream::VideoBufferSetSize(frame.buffer, 8);
    live_stream::FramePayload payload;
    if (Expect(live_stream::EncodedFrameRefCopy(&payload.encoded_frame,
                                                &frame))) {
        return 1;
    }
    payload.has_nal_units = true;
    service->OnFrame(payload);
    live_stream::FramePayloadUnref(&payload);
    live_stream::EncodedFrameUnref(&frame);

    service->Stop();
    if (Expect(service->CreatePeer(create_request).peer_id.empty())) {
        return 1;
    }
    return 0;
}
