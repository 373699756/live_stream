#ifndef LIVE_STREAM_RTSP_SERVICE_H_
#define LIVE_STREAM_RTSP_SERVICE_H_

#include "media/encoded_frame.h"
#include "media/stream_types.h"

#include <cstdint>
#include <memory>
#include <string>

namespace live_stream {

class IAuthService;
class IEventService;
class MediaService;
class NetEngine;

enum class RtspTransportMode {
    kTcpInterleaved = 0,
    kUdp,
};

enum class RtspAdaptiveEventType {
    kSample = 0,
    kFrameDropped,
    kSlowClientClosed,
    kKeyFrameRequested,
};

enum class RtspAdaptiveActionType {
    kNone = 0,
    kRequestKeyFrame,
    kPreferSubStream,
    kReduceBitrate,
    kReduceFrameRate,
};

struct RtspListenAddress {
    std::string ip;
    uint16_t port = 0;
};

struct RtspServiceOptions {
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
    VideoCodec main_stream_codec = VideoCodec::kH264;
    VideoCodec sub_stream_codec = VideoCodec::kH264;
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

struct RtspServiceStats {
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

struct RtspAdaptiveSample {
    RtspAdaptiveEventType event = RtspAdaptiveEventType::kSample;
    RtspSessionStats session;
};

struct RtspAdaptiveAction {
    RtspAdaptiveActionType type = RtspAdaptiveActionType::kNone;
    StreamId stream_id = StreamId::kMain;
    uint32_t target_bitrate_kbps = 0;
    uint32_t target_fps = 0;
};

class IRtspFrameSink {
 public:
    virtual ~IRtspFrameSink() = default;

    virtual bool OnEncodedFrame(const EncodedFrame& frame) = 0;
};

class IRtspFrameSource {
 public:
    virtual ~IRtspFrameSource() = default;

    virtual bool AttachSink(StreamId stream_id,
                            IRtspFrameSink* sink) = 0;
    virtual bool DetachSink(StreamId stream_id,
                            IRtspFrameSink* sink) = 0;
    virtual bool RequestKeyFrame(StreamId stream_id) = 0;
};

class IRtspAdaptiveObserver {
 public:
    virtual ~IRtspAdaptiveObserver() = default;

    virtual RtspAdaptiveAction OnRtspAdaptiveSample(
        const RtspAdaptiveSample& sample) = 0;
};

struct RtspServiceDependencies {
    NetEngine* net_engine = nullptr;
    IAuthService* auth_service = nullptr;
    IEventService* event_service = nullptr;
    MediaService* media_service = nullptr;
    IRtspFrameSource* frame_source = nullptr;
    IRtspAdaptiveObserver* adaptive_observer = nullptr;
};

class IRtspService {
 public:
    virtual ~IRtspService() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual RtspListenAddress LocalAddress() const = 0;
    virtual RtspServiceStats GetStats() const = 0;
    virtual bool PushFrame(const EncodedFrame& frame) = 0;
};

std::unique_ptr<IRtspService> CreateRtspService(
    const RtspServiceOptions& options,
    const RtspServiceDependencies& dependencies);

class RtspService {
 public:
    static const char* Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SERVICE_H_
