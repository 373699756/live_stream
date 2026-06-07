#ifndef LIVE_STREAM_NET_ADAPTIVE_NET_ADAPTIVE_H_
#define LIVE_STREAM_NET_ADAPTIVE_NET_ADAPTIVE_H_

#include "media/stream_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IMediaSource;
class IRtsp;
class IWebrtc;
class INetEngine;

enum class NetAdaptivePressureLevel {
    kNormal = 0,
    kWatch,
    kConstrained,
};

enum class NetAdaptivePressureSignal {
    kNone = 0,
    kTcpPendingBytes,
    kSendQueue,
    kMediaSlowReader,
    kWebrtcDroppedFrames,
};

enum class NetAdaptiveRecommendationType {
    kNone = 0,
    kRequestKeyFrame,
    kPreferSubStream,
    kCloseSlowClient,
};

struct NetAdaptiveOptions {
    bool enabled = true;
    uint32_t sample_interval_ms = 1000;
    uint32_t pending_bytes_watch = 256 * 1024;
    uint32_t pending_bytes_constrained = 768 * 1024;
    uint32_t watch_sample_threshold = 2;
    uint32_t constrained_sample_threshold = 2;
    uint32_t recovery_sample_threshold = 5;
    uint32_t send_queue_watch = 32;
    uint32_t send_queue_constrained = 96;
    uint32_t slow_readers_watch = 1;
    uint32_t slow_readers_constrained = 2;
    uint32_t webrtc_dropped_frames_watch = 1;
    uint32_t webrtc_dropped_frames_constrained = 8;
    uint32_t recommendation_cooldown_ms = 5000;
    uint32_t recommendation_history_limit = 64;
};

struct NetAdaptiveDependencies {
    INetEngine *net_engine = nullptr;
    IRtsp *rtsp = nullptr;
    IWebrtc *webrtc = nullptr;
    IMediaSource *media_source = nullptr;
};

struct NetAdaptiveRecommendation {
    NetAdaptiveRecommendationType type = NetAdaptiveRecommendationType::kNone;
    NetAdaptivePressureLevel level = NetAdaptivePressureLevel::kNormal;
    std::string protocol;
    std::string target;
    StreamId stream_id = StreamId::kMain;
    std::string reason;
    NetAdaptivePressureSignal pressure_signal =
        NetAdaptivePressureSignal::kNone;
    uint32_t pressure_value = 0;
    uint32_t pressure_value_ewma = 0;
    uint32_t pending_bytes = 0;
    uint32_t pending_bytes_ewma = 0;
    uint32_t consecutive_watch_samples = 0;
    uint32_t consecutive_constrained_samples = 0;
    int64_t recommended_at_ms = 0;
};

struct NetAdaptiveStats {
    bool enabled = false;
    NetAdaptivePressureLevel level = NetAdaptivePressureLevel::kNormal;
    uint32_t sampled_connections = 0;
    uint32_t tracked_targets = 0;
    uint32_t watch_targets = 0;
    uint32_t recovering_targets = 0;
    uint32_t constrained_connections = 0;
    uint32_t constrained_targets = 0;
    uint32_t stream_decisions = 0;
    uint32_t recovering_streams = 0;
    uint32_t active_rtsp_sessions = 0;
    uint32_t active_webrtc_peers = 0;
    uint32_t slow_media_readers = 0;
    uint64_t samples = 0;
};

struct NetAdaptiveTargetState {
    NetAdaptivePressureLevel level = NetAdaptivePressureLevel::kNormal;
    std::string protocol;
    std::string target;
    StreamId stream_id = StreamId::kMain;
    NetAdaptivePressureSignal pressure_signal =
        NetAdaptivePressureSignal::kNone;
    uint32_t pressure_value = 0;
    uint32_t pressure_value_ewma = 0;
    uint32_t pending_bytes = 0;
    uint32_t pending_bytes_ewma = 0;
    uint32_t consecutive_watch_samples = 0;
    uint32_t consecutive_constrained_samples = 0;
    uint32_t consecutive_normal_samples = 0;
    int64_t pressure_started_at_ms = 0;
    int64_t normal_since_ms = 0;
    int64_t last_seen_ms = 0;
    int64_t last_recommendation_ms = 0;
};

struct NetAdaptiveStreamDecision {
    StreamId stream_id = StreamId::kMain;
    NetAdaptivePressureLevel level = NetAdaptivePressureLevel::kNormal;
    bool should_request_key_frame = false;
    bool should_prefer_sub_stream = false;
    bool should_close_slow_clients = false;
    bool may_restore_main_stream = false;
    std::string reason;
    uint32_t tracked_targets = 0;
    uint32_t watch_targets = 0;
    uint32_t constrained_targets = 0;
    uint32_t peak_pending_bytes_ewma = 0;
    uint32_t peak_pressure_value_ewma = 0;
    uint32_t slow_media_readers = 0;
    uint32_t webrtc_dropped_frames_delta = 0;
    int64_t updated_at_ms = 0;
    int64_t pressure_started_at_ms = 0;
    int64_t normal_since_ms = 0;
};

class INetAdaptive {
public:
    virtual ~INetAdaptive() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual NetAdaptiveStats GetStats() const = 0;
    virtual std::vector<NetAdaptiveRecommendation>
    GetRecommendations() const = 0;
    virtual std::vector<NetAdaptiveRecommendation>
    GetRecommendationHistory() const = 0;
    virtual std::vector<NetAdaptiveTargetState>
    GetTargetStates() const = 0;
    virtual std::vector<NetAdaptiveStreamDecision>
    GetStreamDecisions() const = 0;
};

std::unique_ptr<INetAdaptive> CreateNetAdaptive(
    const NetAdaptiveOptions &options,
    const NetAdaptiveDependencies &dependencies);

class NetAdaptive {
public:
    static const char *Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_NET_ADAPTIVE_NET_ADAPTIVE_H_
