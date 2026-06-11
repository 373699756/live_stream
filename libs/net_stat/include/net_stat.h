#ifndef LIVE_STREAM_NET_STAT_NET_STAT_H_
#define LIVE_STREAM_NET_STAT_NET_STAT_H_

#include "media/media_streams.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class IRtsp;
class IWebrtc;
class INetEngine;

enum class NetPressureLevel {
    kNormal = 0,
    kWatch,
    kConstrained,
};

enum class NetPressureSignal {
    kNone = 0,
    kTcpPendingBytes,
    kSendQueue,
    kMediaSlowReader,
    kWebrtcDroppedFrames,
};

enum class NetRecommendationType {
    kNone = 0,
    kRequestKeyFrame,
    kPreferSubStream,
    kCloseSlowClient,
};

struct NetStatOptions {
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

struct NetStatDependencies {
    INetEngine *net_engine = nullptr;
    IRtsp *rtsp = nullptr;
    IWebrtc *webrtc = nullptr;
    MediaStreams *media_streams = nullptr;
};

struct NetRecommendation {
    NetRecommendationType type = NetRecommendationType::kNone;
    NetPressureLevel level = NetPressureLevel::kNormal;
    std::string protocol;
    std::string target;
    StreamId stream_id = StreamId::kMain;
    std::string reason;
    NetPressureSignal pressure_signal = NetPressureSignal::kNone;
    uint32_t pressure_value = 0;
    uint32_t pressure_value_ewma = 0;
    uint32_t pending_bytes = 0;
    uint32_t pending_bytes_ewma = 0;
    uint32_t consecutive_watch_samples = 0;
    uint32_t consecutive_constrained_samples = 0;
    int64_t recommended_at_ms = 0;
};

struct NetStatSnapshot {
    bool enabled = false;
    NetPressureLevel level = NetPressureLevel::kNormal;
    uint32_t sampled_connections = 0;
    uint32_t tracked_targets = 0;
    uint32_t watch_targets = 0;
    uint32_t recovering_targets = 0;
    uint32_t constrained_connections = 0;
    uint32_t constrained_targets = 0;
    uint32_t pressure_streams = 0;
    uint32_t recovering_streams = 0;
    uint32_t active_rtsp_sessions = 0;
    uint32_t active_webrtc_peers = 0;
    uint32_t slow_media_readers = 0;
    uint64_t samples = 0;
};

struct NetPressureTarget {
    NetPressureLevel level = NetPressureLevel::kNormal;
    std::string protocol;
    std::string target;
    StreamId stream_id = StreamId::kMain;
    NetPressureSignal pressure_signal = NetPressureSignal::kNone;
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

struct NetStreamPressure {
    StreamId stream_id = StreamId::kMain;
    NetPressureLevel level = NetPressureLevel::kNormal;
    bool request_key_frame = false;
    bool prefer_sub_stream = false;
    bool close_slow_clients = false;
    bool can_restore_main_stream = false;
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

class INetStat {
public:
    virtual ~INetStat() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual NetStatSnapshot GetSnapshot() const = 0;
    virtual std::vector<NetRecommendation>
    GetRecommendations() const = 0;
    virtual std::vector<NetRecommendation>
    GetRecommendationHistory() const = 0;
    virtual std::vector<NetPressureTarget>
    GetPressureTargets() const = 0;
    virtual std::vector<NetStreamPressure>
    GetStreamPressures() const = 0;
};

std::unique_ptr<INetStat> CreateNetStat(
    const NetStatOptions &options,
    const NetStatDependencies &dependencies);

class NetStat {
public:
    static const char *Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_NET_STAT_NET_STAT_H_
