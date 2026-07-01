#include "net_stat.h"

#include "event.h"
#include "infra/log.h"
#include "infra/time.h"
#include "socket_io.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char *kModuleName = "net_stat";
constexpr size_t kMaxSlowClients = 16;
constexpr uint32_t kEwmaNumerator = 3;
constexpr uint32_t kEwmaDenominator = 4;
constexpr int64_t kQueueRecordExpireMs = 30000;

enum class QueueMetric {
    kPendingBytes,
    kSendQueue,
};

NetQueueLevel MaxLevel(NetQueueLevel left, NetQueueLevel right) {
    return static_cast<int>(left) >= static_cast<int>(right) ? left : right;
}

std::string QueueKey(const std::string &key_prefix,
                     const std::string &protocol,
                     const std::string &remote_endpoint) {
    return key_prefix + ":" + protocol + ":" + remote_endpoint;
}

NetMetric NetMetricForQueueMetric(QueueMetric metric) {
    switch (metric) {
        case QueueMetric::kPendingBytes:
            return NetMetric::kTcpPendingBytes;
        case QueueMetric::kSendQueue:
            return NetMetric::kSendQueue;
    }
    return NetMetric::kNone;
}

uint32_t IncrementedMetric(uint32_t value) {
    if (value == UINT32_MAX) {
        return value;
    }
    return value + 1;
}

uint32_t DecrementedMetric(uint32_t value) {
    if (value == 0) {
        return 0;
    }
    return value - 1;
}

uint32_t EventClientCount(const event::Event &event,
                          uint32_t current_count,
                          bool connected_event) {
    if (event.value > 0) {
        return static_cast<uint32_t>(event.value);
    }
    if (connected_event) {
        return IncrementedMetric(current_count);
    }
    return DecrementedMetric(current_count);
}

struct ConnectionQueueRecord {
    std::string key;
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
    NetQueueLevel level = NetQueueLevel::kNormal;
};

struct ProtocolClientActivity {
    uint32_t active_rtsp_sessions = 0;
    uint32_t open_webrtc_peers = 0;
};

}  // namespace

class NetStatImpl final : public INetStat {
public:
    NetStatImpl(NetStatOptions options,
                ISocketIo *socket_io,
                event::EventCenter *event_center)
        : options_(std::move(options)),
          socket_io_(socket_io),
          event_(event_center) {}

    ~NetStatImpl() override { StopInternal(); }

    bool Start() override {
        if (started_) {
            return true;
        }
        if (!options_.enabled) {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_.enabled = false;
            started_ = true;
            return true;
        }
        if (socket_io_ == nullptr || options_.check_interval_ms == 0) {
            return false;
        }
        if (!SubscribeEvents()) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = false;
            stats_.enabled = true;
        }
        check_thread_ = std::thread(&NetStatImpl::CheckLoop, this);
        started_ = true;
        Info(kModuleName, "started interval_ms=%u",
             static_cast<unsigned>(options_.check_interval_ms));
        return true;
    }

    void Stop() override { StopInternal(); }

    NetStatSnapshot GetSnapshot() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    std::vector<NetSlowClient> GetSlowClients() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return slow_clients_;
    }

    std::vector<NetSlowClient>
    GetSlowClientHistory() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return slow_client_history_;
    }

    std::vector<NetConnectionQueue> GetConnectionQueues() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NetConnectionQueue> connection_queues;
        connection_queues.reserve(connection_queues_.size());
        for (const auto &entry : connection_queues_) {
            connection_queues.push_back(ToConnectionQueue(entry.second));
        }
        return connection_queues;
    }

private:
    bool SubscribeEvents() {
        if (event_ == nullptr) {
            return true;
        }
        const std::vector<event::EventType> event_types = {
            event::EventType::kRtspClientConnected,
            event::EventType::kRtspClientDisconnected,
            event::EventType::kWebRtcClientConnected,
            event::EventType::kWebRtcClientDisconnected,
        };
        event_tokens_.reserve(event_types.size());
        for (event::EventType event_type : event_types) {
            event::EventToken token = event_->Subscribe(
                event_type, this, [this](const event::Event &event) {
                    HandleEvent(event);
                });
            if (!token.valid()) {
                for (event::EventToken &registered_token : event_tokens_) {
                    registered_token.Cancel();
                }
                event_tokens_.clear();
                return false;
            }
            event_tokens_.push_back(std::move(token));
        }
        return true;
    }

    void StopInternal() {
        if (!started_) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_all();
        if (check_thread_.joinable()) {
            check_thread_.join();
        }
        for (event::EventToken &event_token : event_tokens_) {
            event_token.Cancel();
        }
        event_tokens_.clear();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_ = NetStatSnapshot{};
            slow_clients_.clear();
            slow_client_history_.clear();
            connection_queues_.clear();
            protocol_activity_ = ProtocolClientActivity{};
            last_published_level_ = NetQueueLevel::kNormal;
            stopping_ = false;
        }
        started_ = false;
    }

    void HandleEvent(const event::Event &event) {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (event.type) {
            case event::EventType::kRtspClientConnected:
                protocol_activity_.active_rtsp_sessions =
                    EventClientCount(
                        event,
                        protocol_activity_.active_rtsp_sessions, true);
                break;
            case event::EventType::kRtspClientDisconnected:
                protocol_activity_.active_rtsp_sessions =
                    EventClientCount(
                        event,
                        protocol_activity_.active_rtsp_sessions, false);
                break;
            case event::EventType::kWebRtcClientConnected:
                protocol_activity_.open_webrtc_peers =
                    EventClientCount(event,
                                     protocol_activity_.open_webrtc_peers,
                                     true);
                break;
            case event::EventType::kWebRtcClientDisconnected:
                protocol_activity_.open_webrtc_peers =
                    EventClientCount(event,
                                     protocol_activity_.open_webrtc_peers,
                                     false);
                break;
            default:
                break;
        }
    }

    void CheckLoop() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (condition_.wait_for(
                        lock,
                        std::chrono::milliseconds(options_.check_interval_ms),
                        [this]() { return stopping_; })) {
                    return;
                }
            }
            Check();
        }
    }

    void Check() {
        NetStatSnapshot next_stats;
        std::vector<NetSlowClient> next_slow_clients;
        const int64_t now_ms = infra::Time::MonotonicMillis();
        next_stats.enabled = options_.enabled;

        CheckConnections(now_ms, next_stats, next_slow_clients);

        bool publish_queue_event = false;
        event::Event queue_event;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            next_stats.active_rtsp_sessions =
                protocol_activity_.active_rtsp_sessions;
            next_stats.open_webrtc_peers =
                protocol_activity_.open_webrtc_peers;
            ExpireIdleQueueRecords(now_ms);
            FillQueueStats(next_stats);
            next_stats.checks = stats_.checks + 1;
            const NetQueueLevel previous_level = stats_.level;
            stats_ = next_stats;
            slow_clients_ = next_slow_clients;
            AppendSlowClientHistory(next_slow_clients);
            publish_queue_event =
                BuildQueueChangeEvent(previous_level, stats_,
                                         queue_event);
        }
        if (publish_queue_event && event_ != nullptr) {
            static_cast<void>(event_->Publish(queue_event));
        }
    }

    void CheckConnections(int64_t now_ms,
                          NetStatSnapshot &stats,
                          std::vector<NetSlowClient> &slow_clients) {
        if (socket_io_ == nullptr) {
            return;
        }
        const std::vector<SocketConnectionInfo> connections =
            socket_io_->ListConnectionInfo();
        for (const SocketConnectionInfo &connection : connections) {
            if (!connection.open) {
                continue;
            }
            ++stats.checked_connections;
            if (ShouldSkipSocketConnection(connection)) {
                continue;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            ConnectionQueueRecord &state =
                UpdateConnectionQueue(connection, now_ms);
            stats.level = MaxLevel(stats.level, state.level);
            if (state.level == NetQueueLevel::kCritical) {
                ++stats.critical_connections;
                AddSlowClientIfReady(
                    slow_clients, state,
                    NetSlowClientType::kCloseSlowClient, now_ms,
                    SlowClientMsg(state, true));
            }
        }
    }

    bool ShouldSkipSocketConnection(
        const SocketConnectionInfo &connection) const {
        if (connection.owner_protocol.empty()) {
            return true;
        }
        return connection.owner_protocol == "onvif";
    }

    ConnectionQueueRecord &UpdateConnectionQueue(
        const SocketConnectionInfo &connection,
        int64_t now_ms) {
        const std::string remote_endpoint = ConnectionEndpoint(connection);
        ConnectionQueueRecord &pending_state =
            UpdateQueueRecord("socket_io", connection.owner_protocol,
                              remote_endpoint,
                              QueueMetric::kPendingBytes,
                              connection.pending_bytes,
                              connection.pending_bytes, now_ms);
        if (connection.send_queue_length == 0) {
            MaybeClearQueueRecord("send_queue",
                                  connection.owner_protocol, remote_endpoint,
                                  QueueMetric::kSendQueue,
                                  connection.pending_bytes, now_ms);
            return pending_state;
        }
        ConnectionQueueRecord &queue_state =
            UpdateQueueRecord("send_queue", connection.owner_protocol,
                              remote_endpoint, QueueMetric::kSendQueue,
                              connection.send_queue_length,
                              connection.pending_bytes, now_ms);
        if (static_cast<int>(pending_state.level) >=
            static_cast<int>(queue_state.level)) {
            return pending_state;
        }
        return queue_state;
    }

    std::string ConnectionEndpoint(const SocketConnectionInfo &connection) const {
        if (!connection.remote_address.ip.empty() &&
            connection.remote_address.port != 0) {
            return connection.remote_address.ip + ":" +
                   std::to_string(connection.remote_address.port);
        }
        if (!connection.remote_address.ip.empty()) {
            return connection.remote_address.ip;
        }
        return std::to_string(connection.connection_id);
    }

    ConnectionQueueRecord &UpdateQueueRecord(
        const std::string &key_prefix,
        const std::string &protocol,
        const std::string &remote_endpoint,
        QueueMetric metric,
        uint32_t metric_value,
        uint32_t pending_bytes,
        int64_t now_ms) {
        const std::string key = QueueKey(key_prefix, protocol,
                                         remote_endpoint);
        ConnectionQueueRecord &state = connection_queues_[key];
        if (state.key.empty()) {
            state.key = key;
            state.protocol = protocol;
            state.remote_endpoint = remote_endpoint;
            state.metric = NetMetricForQueueMetric(metric);
            state.smoothed_metric_value = metric_value;
            state.smoothed_pending_bytes = pending_bytes;
        } else {
            state.metric = NetMetricForQueueMetric(metric);
            state.smoothed_metric_value =
                SmoothValue(state.smoothed_metric_value, metric_value);
            state.smoothed_pending_bytes =
                SmoothValue(state.smoothed_pending_bytes, pending_bytes);
        }
        state.metric_value = metric_value;
        state.pending_bytes = pending_bytes;
        state.last_checked_ms = now_ms;

        const uint32_t warning_threshold = WarningThreshold(metric);
        const uint32_t critical_threshold = CriticalThreshold(metric);
        const NetQueueLevel previous_level = state.level;
        if (critical_threshold != 0 &&
            state.smoothed_metric_value >= critical_threshold) {
            state.level = NetQueueLevel::kCritical;
            ++state.consecutive_critical_checks;
            ++state.consecutive_warning_checks;
            state.consecutive_normal_checks = 0;
            state.normal_since_ms = 0;
            if (state.queue_since_ms == 0) {
                state.queue_since_ms = now_ms;
            }
        } else if (warning_threshold != 0 &&
                   state.smoothed_metric_value >= warning_threshold) {
            state.level = NetQueueLevel::kWarning;
            state.consecutive_critical_checks = 0;
            ++state.consecutive_warning_checks;
            state.consecutive_normal_checks = 0;
            state.normal_since_ms = 0;
            if (state.queue_since_ms == 0) {
                state.queue_since_ms = now_ms;
            }
        } else {
            state.level = NetQueueLevel::kNormal;
            state.consecutive_critical_checks = 0;
            state.consecutive_warning_checks = 0;
            ++state.consecutive_normal_checks;
            if (previous_level != NetQueueLevel::kNormal) {
                state.normal_since_ms = now_ms;
            }
            if (state.consecutive_normal_checks >=
                options_.recovery_check_threshold) {
                state.queue_since_ms = 0;
            }
        }
        return state;
    }

    void MaybeClearQueueRecord(const std::string &key_prefix,
                               const std::string &protocol,
                               const std::string &remote_endpoint,
                               QueueMetric metric,
                               uint32_t pending_bytes,
                               int64_t now_ms) {
        const std::string key = QueueKey(key_prefix, protocol,
                                         remote_endpoint);
        if (connection_queues_.find(key) == connection_queues_.end()) {
            return;
        }
        UpdateQueueRecord(key_prefix, protocol, remote_endpoint,
                          metric, 0, pending_bytes, now_ms);
    }

    uint32_t SmoothValue(uint32_t previous, uint32_t value) const {
        const uint64_t weighted =
            static_cast<uint64_t>(previous) * kEwmaNumerator +
            static_cast<uint64_t>(value);
        return static_cast<uint32_t>(weighted / kEwmaDenominator);
    }

    uint32_t WarningThreshold(QueueMetric metric) const {
        switch (metric) {
            case QueueMetric::kPendingBytes:
                return options_.pending_bytes_warning;
            case QueueMetric::kSendQueue:
                return options_.send_queue_warning;
        }
        return 0;
    }

    uint32_t CriticalThreshold(QueueMetric metric) const {
        switch (metric) {
            case QueueMetric::kPendingBytes:
                return options_.pending_bytes_critical;
            case QueueMetric::kSendQueue:
                return options_.send_queue_critical;
        }
        return 0;
    }

    std::string SlowClientMsg(const ConnectionQueueRecord &state,
                              bool critical) const {
        switch (state.metric) {
            case NetMetric::kTcpPendingBytes:
                return critical ? "tcp_pending_bytes_high"
                                : "tcp_pending_bytes_warning";
            case NetMetric::kSendQueue:
                return critical ? "tcp_send_queue_high"
                                : "tcp_send_queue_warning";
            case NetMetric::kNone:
                break;
        }
        return "queue_observed";
    }

    void AddSlowClientIfReady(
        std::vector<NetSlowClient> &slow_clients,
        ConnectionQueueRecord &state,
        NetSlowClientType type,
        int64_t now_ms,
        const std::string &msg) const {
        if (slow_clients.size() >= kMaxSlowClients) {
            return;
        }
        const bool enough_critical =
            state.level == NetQueueLevel::kCritical &&
            state.consecutive_critical_checks >=
                options_.critical_check_threshold;
        if (!enough_critical) {
            return;
        }
        if (state.last_slow_client_ms != 0 &&
            now_ms - state.last_slow_client_ms <
                static_cast<int64_t>(options_.slow_client_cooldown_ms)) {
            return;
        }
        NetSlowClient slow_client;
        slow_client.type = type;
        slow_client.level = state.level;
        slow_client.protocol = state.protocol;
        slow_client.remote_endpoint = state.remote_endpoint;
        slow_client.msg = msg;
        slow_client.metric = state.metric;
        slow_client.metric_value = state.metric_value;
        slow_client.smoothed_metric_value = state.smoothed_metric_value;
        slow_client.pending_bytes = state.pending_bytes;
        slow_client.smoothed_pending_bytes = state.smoothed_pending_bytes;
        slow_client.consecutive_warning_checks =
            state.consecutive_warning_checks;
        slow_client.consecutive_critical_checks =
            state.consecutive_critical_checks;
        slow_client.found_at_ms = now_ms;
        slow_clients.push_back(slow_client);
        state.last_slow_client_ms = now_ms;
    }

    void AppendSlowClientHistory(
        const std::vector<NetSlowClient> &slow_clients) {
        if (slow_clients.empty() ||
            options_.slow_client_history_limit == 0) {
            return;
        }
        for (const NetSlowClient &slow_client :
             slow_clients) {
            slow_client_history_.push_back(slow_client);
        }
        while (slow_client_history_.size() >
               options_.slow_client_history_limit) {
            slow_client_history_.erase(slow_client_history_.begin());
        }
    }

    void ExpireIdleQueueRecords(int64_t now_ms) {
        for (auto it = connection_queues_.begin();
             it != connection_queues_.end();) {
            if (now_ms - it->second.last_checked_ms > kQueueRecordExpireMs) {
                it = connection_queues_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void FillQueueStats(NetStatSnapshot &stats) const {
        stats.tracked_connection_queues =
            static_cast<uint32_t>(connection_queues_.size());
        for (const auto &entry : connection_queues_) {
            if (entry.second.level == NetQueueLevel::kWarning) {
                ++stats.warning_connection_queues;
            } else if (entry.second.level ==
                       NetQueueLevel::kCritical) {
                ++stats.critical_connection_queues;
            }
            if (entry.second.level == NetQueueLevel::kNormal &&
                entry.second.consecutive_normal_checks > 0 &&
                entry.second.normal_since_ms != 0) {
                ++stats.recovering_connection_queues;
            }
        }
    }

    NetConnectionQueue ToConnectionQueue(
        const ConnectionQueueRecord &source) const {
        NetConnectionQueue queue;
        queue.level = source.level;
        queue.protocol = source.protocol;
        queue.remote_endpoint = source.remote_endpoint;
        queue.metric = source.metric;
        queue.metric_value = source.metric_value;
        queue.smoothed_metric_value = source.smoothed_metric_value;
        queue.pending_bytes = source.pending_bytes;
        queue.smoothed_pending_bytes = source.smoothed_pending_bytes;
        queue.consecutive_warning_checks = source.consecutive_warning_checks;
        queue.consecutive_critical_checks =
            source.consecutive_critical_checks;
        queue.consecutive_normal_checks = source.consecutive_normal_checks;
        queue.queue_since_ms = source.queue_since_ms;
        queue.normal_since_ms = source.normal_since_ms;
        queue.last_checked_ms = source.last_checked_ms;
        queue.last_slow_client_ms = source.last_slow_client_ms;
        return queue;
    }

    bool BuildQueueChangeEvent(
        NetQueueLevel previous_level,
        const NetStatSnapshot &stats,
        event::Event &queue_event) {
        if (event_ == nullptr || previous_level == stats.level ||
            last_published_level_ == stats.level) {
            return false;
        }
        queue_event.type = event::EventType::kNetQueueChanged;
        queue_event.source = kModuleName;
        queue_event.target = "connections";
        queue_event.msg = QueueLevelMsg(stats.level);
        queue_event.value =
            static_cast<int32_t>(stats.tracked_connection_queues);
        queue_event.level = static_cast<uint8_t>(stats.level);
        last_published_level_ = stats.level;
        return true;
    }

    const char *QueueLevelMsg(NetQueueLevel level) const {
        switch (level) {
            case NetQueueLevel::kNormal:
                return "net_queue_normal";
            case NetQueueLevel::kWarning:
                return "net_queue_warning";
            case NetQueueLevel::kCritical:
                return "net_queue_critical";
        }
        return "net_queue_unknown";
    }

    NetStatOptions options_;
    ISocketIo *socket_io_ = nullptr;
    event::EventCenter *event_ = nullptr;
    std::vector<event::EventToken> event_tokens_;
    bool started_ = false;
    bool stopping_ = false;
    ProtocolClientActivity protocol_activity_;
    NetQueueLevel last_published_level_ = NetQueueLevel::kNormal;
    std::map<std::string, ConnectionQueueRecord> connection_queues_;
    std::thread check_thread_;
    std::condition_variable condition_;
    mutable std::mutex mutex_;
    NetStatSnapshot stats_;
    std::vector<NetSlowClient> slow_clients_;
    std::vector<NetSlowClient> slow_client_history_;
};

std::unique_ptr<INetStat> CreateNetStat(
    const NetStatOptions &options,
    ISocketIo *socket_io,
    event::EventCenter *event_center) {
    return std::unique_ptr<INetStat>(
        new NetStatImpl(options, socket_io, event_center));
}

const char *NetStat::Name() { return kModuleName; }

}  // namespace live_stream
