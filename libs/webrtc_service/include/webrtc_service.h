#ifndef LIVE_STREAM_WEBRTC_SERVICE_H_
#define LIVE_STREAM_WEBRTC_SERVICE_H_

#include "media_source.h"

#include "media/stream_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class NetEngine;

enum class WebrtcPeerState {
    kCreated = 0,
    kOfferReceived,
    kConnecting,
    kConnected,
    kClosing,
    kClosed,
    kFailed,
};

struct WebrtcIceServer {
    std::string url;
    std::string username;
    std::string credential;
};

struct WebrtcServiceOptions {
    bool enabled = true;
    uint32_t max_peers = 4;
    uint32_t session_timeout_ms = 30000;
    uint32_t send_queue_capacity = 128;
    uint32_t send_worker_count = 1;
    uint16_t local_port_base = 16000;
    bool prefer_tcp = false;
    std::string public_ip;
    std::vector<WebrtcIceServer> ice_servers;
};

struct WebrtcServiceDependencies {
    IMediaFrameSource *media_source = nullptr;
    NetEngine *net_engine = nullptr;
    bool use_fake_engine = false;
};

struct WebrtcCreatePeerRequest {
    StreamId stream_id = StreamId::kMain;
    std::string client_id;
    std::string session_id;
    std::string user_name;
    std::string client_ip;
};

struct WebrtcPeerInfo {
    std::string peer_id;
    StreamId stream_id = StreamId::kMain;
    VideoCodec codec = VideoCodec::kH264;
    WebrtcPeerState state = WebrtcPeerState::kCreated;
    std::string client_id;
    std::string session_id;
    std::string user_name;
    std::string client_ip;
};

struct WebrtcOfferRequest {
    std::string peer_id;
    std::string sdp;
};

struct WebrtcAnswer {
    std::string peer_id;
    std::string sdp;
};

struct WebrtcIceCandidate {
    std::string peer_id;
    std::string candidate;
    std::string sdp_mid = "0";
    std::string username_fragment;
    int32_t sdp_mline_index = 0;
};

struct WebrtcServiceStats {
    bool enabled = false;
    bool backend_available = false;
    uint32_t active_peers = 0;
    uint32_t max_peers = 0;
    uint64_t total_peers = 0;
    uint64_t offers = 0;
    uint64_t remote_candidates = 0;
    uint64_t sent_frames = 0;
    uint64_t dropped_frames = 0;
};

class IWebrtcService : public IFrameSink {
public:
    ~IWebrtcService() override = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual WebrtcPeerInfo CreatePeer(const WebrtcCreatePeerRequest &request) = 0;
    virtual WebrtcAnswer HandleOffer(const WebrtcOfferRequest &request) = 0;
    virtual bool AddIceCandidate(const WebrtcIceCandidate &candidate) = 0;
    virtual bool ClosePeer(const std::string &peer_id) = 0;
    virtual WebrtcPeerInfo GetPeer(const std::string &peer_id) const = 0;
    virtual WebrtcServiceStats GetStats() const = 0;
    virtual const char *BackendName() const = 0;
};

std::unique_ptr<IWebrtcService>
CreateWebrtcService(const WebrtcServiceOptions &options,
                    const WebrtcServiceDependencies &dependencies);

class WebrtcService {
public:
    static const char *Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SERVICE_H_
