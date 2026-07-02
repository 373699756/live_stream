#ifndef LIVE_STREAM_SOCKET_IO_NET_STAT_H_
#define LIVE_STREAM_SOCKET_IO_NET_STAT_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class ISocketIo;

namespace event {
class EventCenter;
}

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
    // 采样和判级阈值只影响诊断输出，不直接关闭连接；慢客户端动作由上层决定。
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
    // 当前采样周期内达到 critical 且满足连续次数/冷却条件的连接摘要。
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
    // 面向 HTTP/Web 的轻量汇总；连接级细节使用 GetConnectionQueues() 查询。
    bool enabled = false;
    NetQueueLevel level = NetQueueLevel::kNormal;
    uint32_t checked_connections = 0;
    uint32_t tracked_connection_queues = 0;
    uint32_t warning_connection_queues = 0;
    uint32_t recovering_connection_queues = 0;
    uint32_t critical_connections = 0;
    uint32_t critical_connection_queues = 0;
    uint32_t active_rtsp_sessions = 0;
    uint32_t open_webrtc_peers = 0;
    uint32_t slow_clients = 0;
    uint32_t slow_client_history_entries = 0;
    uint64_t checks = 0;
};

struct NetConnectionQueue {
    // 单个连接、单个指标维度的队列状态。一个连接可能同时有 pending bytes
    // 和 send queue 两条记录，调用方按 level/metric 判断主要瓶颈。
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

// socket_io and event_center are non-owning. The composition root must keep
// them alive until the returned INetStat is stopped and destroyed.
std::unique_ptr<INetStat> CreateNetStat(const NetStatOptions &options,
                                        ISocketIo *socket_io,
                                        event::EventCenter *event_center);

class NetStat {
public:
    static const char *Name();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_SOCKET_IO_NET_STAT_H_
