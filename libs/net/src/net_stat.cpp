#include "net_stat.h"

#include "event.h"
#include "infra/log.h"
#include "infra/time.h"
#include "net.h"
#include "runtime.h"

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
constexpr size_t kMaxRecommendations = 16;
constexpr uint32_t kEwmaNumerator = 3;
constexpr uint32_t kEwmaDenominator = 4;
constexpr int64_t kPressureRecordExpireMs = 30000;

enum class PressureMetric {
    kPendingBytes,
    kSendQueue,
};

NetPressureLevel MaxLevel(NetPressureLevel left, NetPressureLevel right) {
    return static_cast<int>(left) >= static_cast<int>(right) ? left : right;
}

std::string PressureKey(const std::string &key_prefix,
                        const std::string &protocol,
                        const std::string &remote_endpoint) {
    return key_prefix + ":" + protocol + ":" + remote_endpoint;
}

NetPressureSignal SignalForMetric(PressureMetric metric) {
    switch (metric) {
        case PressureMetric::kPendingBytes:
            return NetPressureSignal::kTcpPendingBytes;
        case PressureMetric::kSendQueue:
            return NetPressureSignal::kSendQueue;
    }
    return NetPressureSignal::kNone;
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

uint32_t EventActiveValue(const event::Event &event,
                         uint32_t fallback_value) {
    if (event.value >= 0) {
        return static_cast<uint32_t>(event.value);
    }
    return fallback_value;
}

struct ConnectionPressureRecord {
    std::string key;
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
    NetPressureLevel level = NetPressureLevel::kNormal;
};

struct ProtocolClientActivity {
    uint32_t active_rtsp_sessions = 0;
    uint32_t active_webrtc_peers = 0;
};

}  // namespace

class NetStatImpl final : public INetStat {
public:
    explicit NetStatImpl(NetStatOptions options)
        : options_(std::move(options)),
          net_io_(Runtime::NetIo()),
          event_(Runtime::Event()) {}

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
        if (net_io_ == nullptr || options_.check_interval_ms == 0) {
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

    std::vector<NetRecommendation> GetRecommendations() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return recommendations_;
    }

    std::vector<NetRecommendation>
    GetRecommendationHistory() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return recommendation_history_;
    }

    std::vector<NetConnectionPressure> GetConnectionPressures() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NetConnectionPressure> connection_pressures;
        connection_pressures.reserve(connection_pressures_.size());
        for (const auto &entry : connection_pressures_) {
            connection_pressures.push_back(ToConnectionPressure(entry.second));
        }
        return connection_pressures;
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
        event_subscription_ = event_->SubscribeTypes(
            event_types, [this](const event::Event &event) {
                HandleEvent(event);
            });
        return event_subscription_.valid();
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
        event_subscription_.Cancel();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_ = NetStatSnapshot{};
            recommendations_.clear();
            recommendation_history_.clear();
            connection_pressures_.clear();
            protocol_activity_ = ProtocolClientActivity{};
            last_published_level_ = NetPressureLevel::kNormal;
            stopping_ = false;
        }
        started_ = false;
    }

    void HandleEvent(const event::Event &event) {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (event.type) {
            case event::EventType::kRtspClientConnected:
                protocol_activity_.active_rtsp_sessions =
                    EventActiveValue(
                        event,
                        IncrementedMetric(
                            protocol_activity_.active_rtsp_sessions));
                break;
            case event::EventType::kRtspClientDisconnected:
                protocol_activity_.active_rtsp_sessions =
                    EventActiveValue(
                        event,
                        DecrementedMetric(
                            protocol_activity_.active_rtsp_sessions));
                break;
            case event::EventType::kWebRtcClientConnected:
                protocol_activity_.active_webrtc_peers =
                    EventActiveValue(
                        event,
                        IncrementedMetric(
                            protocol_activity_.active_webrtc_peers));
                break;
            case event::EventType::kWebRtcClientDisconnected:
                protocol_activity_.active_webrtc_peers =
                    EventActiveValue(
                        event,
                        DecrementedMetric(
                            protocol_activity_.active_webrtc_peers));
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
        std::vector<NetRecommendation> next_recommendations;
        const int64_t now_ms = infra::Time::MonotonicMillis();
        next_stats.enabled = options_.enabled;

        CheckConnections(now_ms, next_stats, next_recommendations);

        bool publish_pressure_event = false;
        event::Event pressure_event;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            next_stats.active_rtsp_sessions =
                protocol_activity_.active_rtsp_sessions;
            next_stats.active_webrtc_peers =
                protocol_activity_.active_webrtc_peers;
            ExpireIdlePressureRecords(now_ms);
            FillPressureStats(next_stats);
            next_stats.checks = stats_.checks + 1;
            const NetPressureLevel previous_level = stats_.level;
            stats_ = next_stats;
            recommendations_ = next_recommendations;
            AppendRecommendationHistory(next_recommendations);
            publish_pressure_event =
                BuildPressureChangeEvent(previous_level, stats_,
                                         pressure_event);
        }
        if (publish_pressure_event && event_ != nullptr) {
            static_cast<void>(event_->Publish(pressure_event));
        }
    }

    void CheckConnections(int64_t now_ms,
                          NetStatSnapshot &stats,
                          std::vector<NetRecommendation> &recommendations) {
        if (net_io_ == nullptr) {
            return;
        }
        const std::vector<NetConnectionInfo> connections =
            net_io_->ListConnectionInfo();
        for (const NetConnectionInfo &connection : connections) {
            if (!connection.open) {
                continue;
            }
            ++stats.checked_connections;
            if (ShouldSkipNetConnection(connection)) {
                continue;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            ConnectionPressureRecord &state =
                UpdateConnectionPressure(connection, now_ms);
            stats.level = MaxLevel(stats.level, state.level);
            if (state.level == NetPressureLevel::kConstrained) {
                ++stats.constrained_connections;
                AddRecommendationIfReady(
                    recommendations, state,
                    NetRecommendationType::kCloseSlowClient, now_ms,
                    RecommendationReason(state, true));
            }
        }
    }

    bool ShouldSkipNetConnection(
        const NetConnectionInfo &connection) const {
        if (connection.owner_protocol.empty()) {
            return true;
        }
        return connection.owner_protocol == "onvif";
    }

    ConnectionPressureRecord &UpdateConnectionPressure(
        const NetConnectionInfo &connection,
        int64_t now_ms) {
        const std::string remote_endpoint = ConnectionEndpoint(connection);
        ConnectionPressureRecord &pending_state =
            UpdatePressureRecord("net", connection.owner_protocol,
                                 remote_endpoint,
                                 PressureMetric::kPendingBytes,
                                 connection.pending_bytes,
                                 connection.pending_bytes, now_ms);
        if (connection.send_queue_length == 0) {
            MaybeClearPressureRecord("net_queue",
                                     connection.owner_protocol, remote_endpoint,
                                     PressureMetric::kSendQueue,
                                     connection.pending_bytes, now_ms);
            return pending_state;
        }
        ConnectionPressureRecord &queue_state =
            UpdatePressureRecord("net_queue", connection.owner_protocol,
                                 remote_endpoint, PressureMetric::kSendQueue,
                                 connection.send_queue_length,
                                 connection.pending_bytes, now_ms);
        if (static_cast<int>(pending_state.level) >=
            static_cast<int>(queue_state.level)) {
            return pending_state;
        }
        return queue_state;
    }

    std::string ConnectionEndpoint(const NetConnectionInfo &connection) const {
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

    ConnectionPressureRecord &UpdatePressureRecord(
        const std::string &key_prefix,
        const std::string &protocol,
        const std::string &remote_endpoint,
        PressureMetric metric,
        uint32_t pressure_value,
        uint32_t pending_bytes,
        int64_t now_ms) {
        const std::string key = PressureKey(key_prefix, protocol,
                                            remote_endpoint);
        ConnectionPressureRecord &state = connection_pressures_[key];
        if (state.key.empty()) {
            state.key = key;
            state.protocol = protocol;
            state.remote_endpoint = remote_endpoint;
            state.signal = SignalForMetric(metric);
            state.smoothed_pressure_value = pressure_value;
            state.smoothed_pending_bytes = pending_bytes;
        } else {
            state.signal = SignalForMetric(metric);
            state.smoothed_pressure_value =
                SmoothValue(state.smoothed_pressure_value, pressure_value);
            state.smoothed_pending_bytes =
                SmoothValue(state.smoothed_pending_bytes, pending_bytes);
        }
        state.pressure_value = pressure_value;
        state.pending_bytes = pending_bytes;
        state.last_checked_ms = now_ms;

        const uint32_t watch_threshold = WatchThreshold(metric);
        const uint32_t constrained_threshold = ConstrainedThreshold(metric);
        const NetPressureLevel previous_level = state.level;
        if (constrained_threshold != 0 &&
            state.smoothed_pressure_value >= constrained_threshold) {
            state.level = NetPressureLevel::kConstrained;
            ++state.consecutive_constrained_checks;
            ++state.consecutive_watch_checks;
            state.consecutive_normal_checks = 0;
            state.normal_since_ms = 0;
            if (state.pressure_since_ms == 0) {
                state.pressure_since_ms = now_ms;
            }
        } else if (watch_threshold != 0 &&
                   state.smoothed_pressure_value >= watch_threshold) {
            state.level = NetPressureLevel::kWatch;
            state.consecutive_constrained_checks = 0;
            ++state.consecutive_watch_checks;
            state.consecutive_normal_checks = 0;
            state.normal_since_ms = 0;
            if (state.pressure_since_ms == 0) {
                state.pressure_since_ms = now_ms;
            }
        } else {
            state.level = NetPressureLevel::kNormal;
            state.consecutive_constrained_checks = 0;
            state.consecutive_watch_checks = 0;
            ++state.consecutive_normal_checks;
            if (previous_level != NetPressureLevel::kNormal) {
                state.normal_since_ms = now_ms;
            }
            if (state.consecutive_normal_checks >=
                options_.recovery_check_threshold) {
                state.pressure_since_ms = 0;
            }
        }
        return state;
    }

    void MaybeClearPressureRecord(const std::string &key_prefix,
                                  const std::string &protocol,
                                  const std::string &remote_endpoint,
                                  PressureMetric metric,
                                  uint32_t pending_bytes,
                                  int64_t now_ms) {
        const std::string key = PressureKey(key_prefix, protocol,
                                            remote_endpoint);
        if (connection_pressures_.find(key) == connection_pressures_.end()) {
            return;
        }
        UpdatePressureRecord(key_prefix, protocol, remote_endpoint,
                             metric, 0, pending_bytes, now_ms);
    }

    uint32_t SmoothValue(uint32_t previous, uint32_t value) const {
        const uint64_t weighted =
            static_cast<uint64_t>(previous) * kEwmaNumerator +
            static_cast<uint64_t>(value);
        return static_cast<uint32_t>(weighted / kEwmaDenominator);
    }

    uint32_t WatchThreshold(PressureMetric metric) const {
        switch (metric) {
            case PressureMetric::kPendingBytes:
                return options_.pending_bytes_watch;
            case PressureMetric::kSendQueue:
                return options_.send_queue_watch;
        }
        return 0;
    }

    uint32_t ConstrainedThreshold(PressureMetric metric) const {
        switch (metric) {
            case PressureMetric::kPendingBytes:
                return options_.pending_bytes_constrained;
            case PressureMetric::kSendQueue:
                return options_.send_queue_constrained;
        }
        return 0;
    }

    std::string RecommendationReason(const ConnectionPressureRecord &state,
                                     bool constrained) const {
        switch (state.signal) {
            case NetPressureSignal::kTcpPendingBytes:
                return constrained ? "tcp_pending_bytes_high"
                                   : "tcp_pending_bytes_watch";
            case NetPressureSignal::kSendQueue:
                return constrained ? "tcp_send_queue_high"
                                   : "tcp_send_queue_watch";
            case NetPressureSignal::kNone:
                break;
        }
        return "pressure_observed";
    }

    void AddRecommendationIfReady(
        std::vector<NetRecommendation> &recommendations,
        ConnectionPressureRecord &state,
        NetRecommendationType type,
        int64_t now_ms,
        const std::string &reason) const {
        if (recommendations.size() >= kMaxRecommendations) {
            return;
        }
        const bool enough_constrained =
            state.level == NetPressureLevel::kConstrained &&
            state.consecutive_constrained_checks >=
                options_.constrained_check_threshold;
        if (!enough_constrained) {
            return;
        }
        if (state.last_recommendation_ms != 0 &&
            now_ms - state.last_recommendation_ms <
                static_cast<int64_t>(options_.recommendation_cooldown_ms)) {
            return;
        }
        NetRecommendation recommendation;
        recommendation.type = type;
        recommendation.level = state.level;
        recommendation.protocol = state.protocol;
        recommendation.remote_endpoint = state.remote_endpoint;
        recommendation.reason = reason;
        recommendation.signal = state.signal;
        recommendation.pressure_value = state.pressure_value;
        recommendation.smoothed_pressure_value = state.smoothed_pressure_value;
        recommendation.pending_bytes = state.pending_bytes;
        recommendation.smoothed_pending_bytes = state.smoothed_pending_bytes;
        recommendation.consecutive_watch_checks =
            state.consecutive_watch_checks;
        recommendation.consecutive_constrained_checks =
            state.consecutive_constrained_checks;
        recommendation.recommended_at_ms = now_ms;
        recommendations.push_back(recommendation);
        state.last_recommendation_ms = now_ms;
    }

    void AppendRecommendationHistory(
        const std::vector<NetRecommendation> &recommendations) {
        if (recommendations.empty() ||
            options_.recommendation_history_limit == 0) {
            return;
        }
        for (const NetRecommendation &recommendation :
             recommendations) {
            recommendation_history_.push_back(recommendation);
        }
        while (recommendation_history_.size() >
               options_.recommendation_history_limit) {
            recommendation_history_.erase(recommendation_history_.begin());
        }
    }

    void ExpireIdlePressureRecords(int64_t now_ms) {
        for (auto it = connection_pressures_.begin();
             it != connection_pressures_.end();) {
            if (now_ms - it->second.last_checked_ms > kPressureRecordExpireMs) {
                it = connection_pressures_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void FillPressureStats(NetStatSnapshot &stats) const {
        stats.tracked_connection_pressures =
            static_cast<uint32_t>(connection_pressures_.size());
        for (const auto &entry : connection_pressures_) {
            if (entry.second.level == NetPressureLevel::kWatch) {
                ++stats.watch_connection_pressures;
            } else if (entry.second.level ==
                       NetPressureLevel::kConstrained) {
                ++stats.constrained_connection_pressures;
            }
            if (entry.second.level == NetPressureLevel::kNormal &&
                entry.second.consecutive_normal_checks > 0 &&
                entry.second.normal_since_ms != 0) {
                ++stats.recovering_connection_pressures;
            }
        }
    }

    NetConnectionPressure ToConnectionPressure(
        const ConnectionPressureRecord &source) const {
        NetConnectionPressure pressure;
        pressure.level = source.level;
        pressure.protocol = source.protocol;
        pressure.remote_endpoint = source.remote_endpoint;
        pressure.signal = source.signal;
        pressure.pressure_value = source.pressure_value;
        pressure.smoothed_pressure_value = source.smoothed_pressure_value;
        pressure.pending_bytes = source.pending_bytes;
        pressure.smoothed_pending_bytes = source.smoothed_pending_bytes;
        pressure.consecutive_watch_checks = source.consecutive_watch_checks;
        pressure.consecutive_constrained_checks =
            source.consecutive_constrained_checks;
        pressure.consecutive_normal_checks = source.consecutive_normal_checks;
        pressure.pressure_since_ms = source.pressure_since_ms;
        pressure.normal_since_ms = source.normal_since_ms;
        pressure.last_checked_ms = source.last_checked_ms;
        pressure.last_recommendation_ms = source.last_recommendation_ms;
        return pressure;
    }

    bool BuildPressureChangeEvent(
        NetPressureLevel previous_level,
        const NetStatSnapshot &stats,
        event::Event &pressure_event) {
        if (event_ == nullptr || previous_level == stats.level ||
            last_published_level_ == stats.level) {
            return false;
        }
        pressure_event.type = event::EventType::kNetPressureChanged;
        pressure_event.source = kModuleName;
        pressure_event.target = "connections";
        pressure_event.message = PressureLevelReason(stats.level);
        pressure_event.value =
            static_cast<int32_t>(stats.tracked_connection_pressures);
        pressure_event.level = static_cast<uint8_t>(stats.level);
        last_published_level_ = stats.level;
        return true;
    }

    const char *PressureLevelReason(NetPressureLevel level) const {
        switch (level) {
            case NetPressureLevel::kNormal:
                return "net_pressure_normal";
            case NetPressureLevel::kWatch:
                return "net_pressure_watch";
            case NetPressureLevel::kConstrained:
                return "net_pressure_constrained";
        }
        return "net_pressure_unknown";
    }

    NetStatOptions options_;
    INetIo *net_io_ = nullptr;
    event::Dispatcher *event_ = nullptr;
    event::Subscription event_subscription_;
    bool started_ = false;
    bool stopping_ = false;
    ProtocolClientActivity protocol_activity_;
    NetPressureLevel last_published_level_ = NetPressureLevel::kNormal;
    std::map<std::string, ConnectionPressureRecord> connection_pressures_;
    std::thread check_thread_;
    std::condition_variable condition_;
    mutable std::mutex mutex_;
    NetStatSnapshot stats_;
    std::vector<NetRecommendation> recommendations_;
    std::vector<NetRecommendation> recommendation_history_;
};

std::unique_ptr<INetStat> CreateNetStat(const NetStatOptions &options) {
    return std::unique_ptr<INetStat>(new NetStatImpl(options));
}

const char *NetStat::Name() { return kModuleName; }

}  // namespace live_stream
