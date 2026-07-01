#ifndef LIVE_STREAM_SOCKET_IO_NET_STAT_H_
#define LIVE_STREAM_SOCKET_IO_NET_STAT_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

enum class NetQueueLevel {
    kNormal = 0,
    kWarning,
    kCritical,
};

enum class NetMetric {
    kNone = 0,
    kTcpPendingBytes,
    kSendQueue,
};

enum class NetSlowClientType {
    kNone = 0,
    kCloseSlowClient,
};

struct NetStatOptions {
    bool enabled = true;
    uint32_t check_interval_ms = 1000;
    uint32_t pending_bytes_warning = 256 * 1024;
    uint32_t pending_bytes_critical = 768 * 1024;
    uint32_t critical_check_threshold = 2;
    uint32_t recovery_check_threshold = 5;
    uint32_t send_queue_warning = 32;
    uint32_t send_queue_critical = 96;
    uint32_t slow_client_cooldown_ms = 5000;
    uint32_t slow_client_history_limit = 64;
};

struct NetSlowClient {
    NetSlowClientType type = NetSlowClientType::kNone;
    NetQueueLevel level = NetQueueLevel::kNormal;
    std::string protocol;
    std::string remote_endpoint;
    std::string msg;
    NetMetric metric = NetMetric::kNone;
    uint32_t metric_value = 0;
    uint32_t smoothed_metric_value = 0;
    uint32_t pending_bytes = 0;
    uint32_t smoothed_pending_bytes = 0;
    uint32_t consecutive_warning_checks = 0;
    uint32_t consecutive_critical_checks = 0;
    int64_t found_at_ms = 0;
};

struct NetStatSnapshot {
    bool enabled = false;
    NetQueueLevel level = NetQueueLevel::kNormal;
    uint32_t checked_connections = 0;
    uint32_t tracked_connection_queues = 0;
    uint32_t warning_connection_queues = 0;
    uint32_t recovering_connection_queues = 0;
    uint32_t critical_connections = 0;
    uint32_t critical_connection_queues = 0;
    uint32_t active_rtsp_sessions = 0;
    uint32_t active_webrtc_peers = 0;
    uint64_t checks = 0;
};

struct NetConnectionQueue {
    NetQueueLevel level = NetQueueLevel::kNormal;
    std::string protocol;
    std::string remote_endpoint;
    NetMetric metric = NetMetric::kNone;
    uint32_t metric_value = 0;
    uint32_t smoothed_metric_value = 0;
    uint32_t pending_bytes = 0;
    uint32_t smoothed_pending_bytes = 0;
    uint32_t consecutive_warning_checks = 0;
    uint32_t consecutive_critical_checks = 0;
    uint32_t consecutive_normal_checks = 0;
    int64_t queue_since_ms = 0;
    int64_t normal_since_ms = 0;
    int64_t last_checked_ms = 0;
    int64_t last_slow_client_ms = 0;
};

class INetStat {
public:
    virtual ~INetStat() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual NetStatSnapshot GetSnapshot() const = 0;
    virtual std::vector<NetSlowClient>
    GetSlowClients() const = 0;
    virtual std::vector<NetSlowClient>
    GetSlowClientHistory() const = 0;
    virtual std::vector<NetConnectionQueue>
    GetConnectionQueues() const = 0;
};

std::unique_ptr<INetStat> CreateNetStat(const NetStatOptions &options);

class NetStat {
public:
    static const char *Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_SOCKET_IO_NET_STAT_H_
