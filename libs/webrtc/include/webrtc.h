#ifndef LIVE_STREAM_WEBRTC_WEBRTC_H_
#define LIVE_STREAM_WEBRTC_WEBRTC_H_

#include "event.h"
#include "media/media_streams.h"
#include "media/stream_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class INetIo;

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

struct WebrtcOptions {
    bool enabled = true;
    uint32_t max_peers = 2;
    uint32_t session_timeout_ms = 30000;
    uint32_t send_queue_capacity = 128;
    uint32_t send_workers = 1;
    uint16_t local_port_base = 16000;
    bool prefer_tcp = false;
    std::string public_ip;
    std::vector<WebrtcIceServer> ice_servers;
};

struct WebrtcDependencies {
    MediaStreams *media_streams = nullptr;
    INetIo *net_io = nullptr;
    event::Loop *net_loop = nullptr;
    event::Dispatcher *event = nullptr;
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
    Codec codec = Codec::kH264;
    WebrtcPeerState state = WebrtcPeerState::kCreated;
    std::string client_id;
    std::string session_id;
    std::string user_name;
    std::string client_ip;
    uint64_t subscription_id = 0;
    bool subscription_open = false;
    uint64_t subscription_generation = 0;
    uint32_t subscription_pending_frames = 0;
    bool subscription_waiting_keyframe = false;
    bool subscription_slow = false;
    std::string subscription_close_reason;
    bool ice_selected = false;
    std::string dtls_state = "new";
    bool srtp_ready = false;
    uint64_t rtp_packets = 0;
    uint64_t rtp_bytes = 0;
    uint64_t rtcp_packets = 0;
    uint64_t rtcp_bytes = 0;
    uint64_t rtcp_pli_packets = 0;
    uint64_t rtcp_fir_packets = 0;
    uint64_t rtcp_nack_packets = 0;
    uint64_t rtcp_transport_cc_packets = 0;
    uint64_t rtcp_keyframe_requests = 0;
    std::string last_error;
    int64_t created_at_ms = 0;
    int64_t updated_at_ms = 0;
};

struct WebrtcOfferRequest {
    std::string peer_id;
    std::string sdp;
};

struct WebrtcAnswer {
    std::string peer_id;
    std::string sdp;
    WebrtcPeerState state = WebrtcPeerState::kCreated;
    std::string error;
};

struct WebrtcIceCandidate {
    std::string peer_id;
    std::string candidate;
    std::string sdp_mid = "0";
    std::string username_fragment;
    int32_t sdp_mline_index = 0;
};

struct WebrtcStats {
    bool enabled = false;
    bool signaling_ready = false;
    bool ice_ready = false;
    bool dtls_ready = false;
    bool srtp_ready = false;
    uint16_t local_port_base = 0;
    uint32_t active_peers = 0;
    uint32_t max_peers = 0;
    uint32_t ice_servers = 0;
    uint64_t total_peers = 0;
    uint64_t offers = 0;
    uint64_t remote_candidates = 0;
    uint32_t selected_ice_pairs = 0;
    uint64_t sent_frames = 0;
    uint64_t dropped_frames = 0;
    uint64_t sent_rtp_packets = 0;
    uint64_t dropped_rtp_packets = 0;
    uint64_t rtcp_packets = 0;
    uint64_t rtcp_bytes = 0;
    uint64_t rtcp_pli_packets = 0;
    uint64_t rtcp_fir_packets = 0;
    uint64_t rtcp_nack_packets = 0;
    uint64_t rtcp_transport_cc_packets = 0;
    uint64_t rtcp_keyframe_requests = 0;
    std::string public_ip;
};

class IWebrtcStatusReader {
public:
    virtual ~IWebrtcStatusReader() = default;

    virtual std::vector<WebrtcPeerInfo> GetPeers() const = 0;
    virtual WebrtcStats GetStats() const = 0;
};

class IWebrtc : public IWebrtcStatusReader {
public:
    ~IWebrtc() override = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool ApplyOptions(const WebrtcOptions &options) = 0;
    virtual WebrtcPeerInfo CreatePeer(const WebrtcCreatePeerRequest &request) = 0;
    virtual WebrtcAnswer HandleOffer(const WebrtcOfferRequest &request) = 0;
    virtual bool AddIceCandidate(const WebrtcIceCandidate &candidate) = 0;
    virtual bool ClosePeer(const std::string &peer_id) = 0;
    virtual WebrtcPeerInfo GetPeer(const std::string &peer_id) const = 0;
};

std::unique_ptr<IWebrtc>
CreateWebrtc(const WebrtcOptions &options,
             const WebrtcDependencies &dependencies);

class Webrtc {
public:
    static const char *Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_WEBRTC_H_
