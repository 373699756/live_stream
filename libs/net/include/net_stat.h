#ifndef LIVE_STREAM_NET_NET_STAT_H_
#define LIVE_STREAM_NET_NET_STAT_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class INetIo;
namespace event {
class Dispatcher;
}

enum class NetPressureLevel {
    kNormal = 0,
    kWatch,
    kConstrained,
};

enum class NetPressureSignal {
    kNone = 0,
    kTcpPendingBytes,
    kSendQueue,
};

enum class NetRecommendationType {
    kNone = 0,
    kCloseSlowClient,
};

struct NetStatOptions {
    bool enabled = true;
    uint32_t check_interval_ms = 1000;
    uint32_t pending_bytes_watch = 256 * 1024;
    uint32_t pending_bytes_constrained = 768 * 1024;
    uint32_t constrained_check_threshold = 2;
    uint32_t recovery_check_threshold = 5;
    uint32_t send_queue_watch = 32;
    uint32_t send_queue_constrained = 96;
    uint32_t recommendation_cooldown_ms = 5000;
    uint32_t recommendation_history_limit = 64;
};

struct NetStatDependencies {
    INetIo *net_io = nullptr;
    event::Dispatcher *event = nullptr;
};

struct NetRecommendation {
    NetRecommendationType type = NetRecommendationType::kNone;
    NetPressureLevel level = NetPressureLevel::kNormal;
    std::string protocol;
    std::string remote_endpoint;
    std::string reason;
    NetPressureSignal signal = NetPressureSignal::kNone;
    uint32_t pressure_value = 0;
    uint32_t smoothed_pressure_value = 0;
    uint32_t pending_bytes = 0;
    uint32_t smoothed_pending_bytes = 0;
    uint32_t consecutive_watch_checks = 0;
    uint32_t consecutive_constrained_checks = 0;
    int64_t recommended_at_ms = 0;
};

struct NetStatSnapshot {
    bool enabled = false;
    NetPressureLevel level = NetPressureLevel::kNormal;
    uint32_t checked_connections = 0;
    uint32_t tracked_connection_pressures = 0;
    uint32_t watch_connection_pressures = 0;
    uint32_t recovering_connection_pressures = 0;
    uint32_t constrained_connections = 0;
    uint32_t constrained_connection_pressures = 0;
    uint32_t active_rtsp_sessions = 0;
    uint32_t active_webrtc_peers = 0;
    uint64_t check_count = 0;
};

struct NetConnectionPressure {
    NetPressureLevel level = NetPressureLevel::kNormal;
    std::string protocol;
    std::string remote_endpoint;
    NetPressureSignal signal = NetPressureSignal::kNone;
    uint32_t pressure_value = 0;
    uint32_t smoothed_pressure_value = 0;
    uint32_t pending_bytes = 0;
    uint32_t smoothed_pending_bytes = 0;
    uint32_t consecutive_watch_checks = 0;
    uint32_t consecutive_constrained_checks = 0;
    uint32_t consecutive_normal_checks = 0;
    int64_t pressure_since_ms = 0;
    int64_t normal_since_ms = 0;
    int64_t last_checked_ms = 0;
    int64_t last_recommendation_ms = 0;
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
    virtual std::vector<NetConnectionPressure>
    GetConnectionPressures() const = 0;
};

std::unique_ptr<INetStat> CreateNetStat(
    const NetStatOptions &options,
    const NetStatDependencies &dependencies);

class NetStat {
public:
    static const char *Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_NET_NET_STAT_H_
