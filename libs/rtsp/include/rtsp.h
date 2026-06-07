#ifndef LIVE_STREAM_RTSP_RTSP_H_
#define LIVE_STREAM_RTSP_RTSP_H_

#include "media/encoded_frame.h"
#include "media/stream_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IAuth;
class IEvent;
class IMediaFrameSource;
class NetEngine;

enum class RtspTransportMode {
    kTcpInterleaved = 0,
    kUdp,
};

struct RtspListenAddress {
    std::string ip;
    uint16_t port = 0;
};

inline const char *RtspStreamPath(StreamId stream_id) {
    return stream_id == StreamId::kSub ? "/live/sub" : "/live/main";
}

inline std::string BuildRtspStreamUrl(const RtspListenAddress &address,
                                      StreamId stream_id,
                                      const std::string &advertise_host) {
    const std::string host =
        advertise_host.empty() ? address.ip : advertise_host;
    if (host.empty() || address.port == 0) {
        return std::string();
    }
    return std::string("rtsp://") + host + ":" +
           std::to_string(address.port) + RtspStreamPath(stream_id);
}

struct RtspOptions {
    std::string listen_ip = "0.0.0.0";
    uint16_t listen_port = 554;
    uint32_t max_sessions = 16;
    uint32_t max_request_bytes = 8 * 1024;
    uint32_t session_timeout_ms = 30000;
    uint32_t rtp_mtu_bytes = 1200;
    uint32_t send_queue_capacity = 128;
    uint32_t send_buffer_limit_bytes = 1024 * 1024;
    uint32_t send_stall_timeout_ms = 5000;
    RtspTransportMode default_transport = RtspTransportMode::kTcpInterleaved;
    bool enable_auth = false;
    VideoCodec main_video_codec = VideoCodec::kH264;
    VideoCodec sub_video_codec = VideoCodec::kH264;
};

struct RtspSessionStats {
    uint64_t session_id = 0;
    StreamId stream_id = StreamId::kMain;
    RtspTransportMode transport = RtspTransportMode::kTcpInterleaved;
    uint32_t pending_bytes = 0;
    uint64_t sent_rtp_packets = 0;
    uint64_t sent_rtp_bytes = 0;
    uint64_t dropped_frames = 0;
};

struct RtspSessionDiagnostics {
    uint64_t session_id = 0;
    StreamId stream_id = StreamId::kMain;
    RtspTransportMode transport = RtspTransportMode::kTcpInterleaved;
    std::string remote_address;
    std::string local_address;
    uint64_t reader_id = 0;
    uint32_t pending_bytes = 0;
    uint64_t rtp_packets = 0;
    uint64_t rtp_bytes = 0;
    std::string close_reason;
};

struct RtspStats {
    uint32_t active_sessions = 0;
    uint64_t total_sessions = 0;
    uint64_t auth_failures = 0;
    uint64_t parse_failures = 0;
    uint64_t tcp_interleaved_sessions = 0;
    uint64_t udp_sessions = 0;
    uint64_t sent_rtp_packets = 0;
    uint64_t sent_rtp_bytes = 0;
    uint64_t dropped_frames = 0;
    uint64_t slow_client_closes = 0;
};

struct RtspDependencies {
    NetEngine* net_engine = nullptr;
    IAuth* auth = nullptr;
    IEvent* event = nullptr;
    IMediaFrameSource* media_source = nullptr;
};

class IRtsp {
public:
    virtual ~IRtsp() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool ApplyOptions(const RtspOptions& options) = 0;
    virtual RtspListenAddress LocalAddress() const = 0;
    virtual RtspStats GetStats() const = 0;
    virtual std::vector<RtspSessionDiagnostics>
    GetSessionDiagnostics() const = 0;
};

std::unique_ptr<IRtsp> CreateRtsp(
    const RtspOptions& options,
    const RtspDependencies& dependencies);

class Rtsp {
public:
    static const char* Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_RTSP_H_
