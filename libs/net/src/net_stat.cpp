#include "net_stat.h"

#include "event.h"
#include "infra/log.h"
#include "infra/time.h"
#include "net.h"

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

constexpr const char *kServiceName = "net_stat";
constexpr size_t kMaxRecommendations = 16;
constexpr uint32_t kEwmaNumerator = 3;
constexpr uint32_t kEwmaDenominator = 4;
constexpr int64_t kTargetIdleExpireMs = 30000;

enum class PressureMetric {
    kPendingBytes,
    kSendQueue,
};

NetPressureLevel MaxLevel(NetPressureLevel left, NetPressureLevel right) {
    return static_cast<int>(left) >= static_cast<int>(right) ? left : right;
}

std::string TargetKey(const std::string &key_prefix,
                      const std::string &protocol,
                      const std::string &target) {
    return key_prefix + ":" + protocol + ":" + target;
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

uint32_t IncrementedCounter(uint32_t value) {
    if (value == UINT32_MAX) {
        return value;
    }
    return value + 1;
}

uint32_t DecrementedCounter(uint32_t value) {
    if (value == 0) {
        return 0;
    }
    return value - 1;
}

uint32_t EventActiveCount(const event::Event &event,
                          uint32_t fallback_value) {
    if (event.value >= 0) {
        return static_cast<uint32_t>(event.value);
    }
    return fallback_value;
}

struct ObservedTargetState {
    std::string key;
    std::string protocol;
    std::string target;
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
    NetPressureLevel level = NetPressureLevel::kNormal;
};

struct ProtocolActivity {
    uint32_t active_rtsp_sessions = 0;
    uint32_t active_webrtc_peers = 0;
};

}  // namespace

class NetStatImpl final : public INetStat {
public:
    NetStatImpl(NetStatOptions options, NetStatDependencies dependencies)
        : options_(std::move(options)),
          net_engine_(dependencies.net_engine),
          event_(dependencies.event) {}

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
        if (net_engine_ == nullptr || options_.sample_interval_ms == 0) {
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
        sample_thread_ = std::thread(&NetStatImpl::SampleLoop, this);
        started_ = true;
        Info(kServiceName, "started interval_ms=%u",
             static_cast<unsigned>(options_.sample_interval_ms));
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

    std::vector<NetPressureTarget> GetPressureTargets() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NetPressureTarget> pressure_targets;
        pressure_targets.reserve(target_states_.size());
        for (const auto &entry : target_states_) {
            pressure_targets.push_back(ToPressureTarget(entry.second));
        }
        return pressure_targets;
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
        if (sample_thread_.joinable()) {
            sample_thread_.join();
        }
        event_subscription_.Cancel();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_ = NetStatSnapshot{};
            recommendations_.clear();
            recommendation_history_.clear();
            target_states_.clear();
            protocol_activity_ = ProtocolActivity{};
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
                    EventActiveCount(
                        event,
                        IncrementedCounter(
                            protocol_activity_.active_rtsp_sessions));
                break;
            case event::EventType::kRtspClientDisconnected:
                protocol_activity_.active_rtsp_sessions =
                    EventActiveCount(
                        event,
                        DecrementedCounter(
                            protocol_activity_.active_rtsp_sessions));
                break;
            case event::EventType::kWebRtcClientConnected:
                protocol_activity_.active_webrtc_peers =
                    EventActiveCount(
                        event,
                        IncrementedCounter(
                            protocol_activity_.active_webrtc_peers));
                break;
            case event::EventType::kWebRtcClientDisconnected:
                protocol_activity_.active_webrtc_peers =
                    EventActiveCount(
                        event,
                        DecrementedCounter(
                            protocol_activity_.active_webrtc_peers));
                break;
            default:
                break;
        }
    }

    void SampleLoop() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (condition_.wait_for(
                        lock,
                        std::chrono::milliseconds(options_.sample_interval_ms),
                        [this]() { return stopping_; })) {
                    return;
                }
            }
            Sample();
        }
    }

    void Sample() {
        NetStatSnapshot next_stats;
        std::vector<NetRecommendation> next_recommendations;
        const int64_t now_ms = infra::Time::MonotonicMillis();
        next_stats.enabled = options_.enabled;

        SampleNet(now_ms, &next_stats, &next_recommendations);

        bool publish_pressure_event = false;
        event::Event pressure_event;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            next_stats.active_rtsp_sessions =
                protocol_activity_.active_rtsp_sessions;
            next_stats.active_webrtc_peers =
                protocol_activity_.active_webrtc_peers;
            ExpireIdleTargets(now_ms);
            FillTargetStats(&next_stats);
            next_stats.samples = stats_.samples + 1;
            const NetPressureLevel previous_level = stats_.level;
            stats_ = next_stats;
            recommendations_ = next_recommendations;
            AppendRecommendationHistory(next_recommendations);
            publish_pressure_event =
                BuildPressureChangeEvent(previous_level, stats_,
                                         &pressure_event);
        }
        if (publish_pressure_event && event_ != nullptr) {
            static_cast<void>(event_->Publish(pressure_event));
        }
    }

    void SampleNet(int64_t now_ms,
                   NetStatSnapshot *stats,
                   std::vector<NetRecommendation> *recommendations) {
        if (net_engine_ == nullptr || stats == nullptr ||
            recommendations == nullptr) {
            return;
        }
        const std::vector<NetConnectionInfo> connections =
            net_engine_->ListConnectionInfo();
        for (const NetConnectionInfo &connection : connections) {
            if (!connection.open) {
                continue;
            }
            ++stats->sampled_connections;
            if (ShouldSkipNetConnection(connection)) {
                continue;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            ObservedTargetState *state = UpdateConnectionTarget(connection,
                                                                now_ms);
            if (state == nullptr) {
                continue;
            }
            stats->level = MaxLevel(stats->level, state->level);
            if (state->level == NetPressureLevel::kConstrained) {
                ++stats->constrained_connections;
                AddRecommendationIfReady(
                    recommendations, state,
                    NetRecommendationType::kCloseSlowClient, now_ms,
                    RecommendationReason(*state, true));
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

    ObservedTargetState *UpdateConnectionTarget(
        const NetConnectionInfo &connection,
        int64_t now_ms) {
        const std::string target = ConnectionTarget(connection);
        ObservedTargetState *pending_state =
            UpdateTarget("net", connection.owner_protocol, target,
                         PressureMetric::kPendingBytes,
                         connection.pending_bytes,
                         connection.pending_bytes, now_ms);
        if (connection.send_queue_length == 0) {
            MaybeUpdateClearedTarget("net_queue",
                                     connection.owner_protocol, target,
                                     PressureMetric::kSendQueue,
                                     connection.pending_bytes, now_ms);
            return pending_state;
        }
        ObservedTargetState *queue_state =
            UpdateTarget("net_queue", connection.owner_protocol, target,
                         PressureMetric::kSendQueue,
                         connection.send_queue_length,
                         connection.pending_bytes, now_ms);
        if (pending_state == nullptr) {
            return queue_state;
        }
        if (queue_state == nullptr ||
            static_cast<int>(pending_state->level) >=
                static_cast<int>(queue_state->level)) {
            return pending_state;
        }
        return queue_state;
    }

    std::string ConnectionTarget(const NetConnectionInfo &connection) const {
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

    ObservedTargetState *UpdateTarget(const std::string &key_prefix,
                                      const std::string &protocol,
                                      const std::string &target,
                                      PressureMetric metric,
                                      uint32_t pressure_value,
                                      uint32_t pending_bytes,
                                      int64_t now_ms) {
        const std::string key = TargetKey(key_prefix, protocol, target);
        ObservedTargetState &state = target_states_[key];
        if (state.key.empty()) {
            state.key = key;
            state.protocol = protocol;
            state.target = target;
            state.pressure_signal = SignalForMetric(metric);
            state.pressure_value_ewma = pressure_value;
            state.pending_bytes_ewma = pending_bytes;
        } else {
            state.pressure_signal = SignalForMetric(metric);
            state.pressure_value_ewma =
                SmoothValue(state.pressure_value_ewma, pressure_value);
            state.pending_bytes_ewma =
                SmoothValue(state.pending_bytes_ewma, pending_bytes);
        }
        state.pressure_value = pressure_value;
        state.pending_bytes = pending_bytes;
        state.last_seen_ms = now_ms;

        const uint32_t watch_threshold = WatchThreshold(metric);
        const uint32_t constrained_threshold = ConstrainedThreshold(metric);
        const NetPressureLevel previous_level = state.level;
        if (constrained_threshold != 0 &&
            state.pressure_value_ewma >= constrained_threshold) {
            state.level = NetPressureLevel::kConstrained;
            ++state.consecutive_constrained_samples;
            ++state.consecutive_watch_samples;
            state.consecutive_normal_samples = 0;
            state.normal_since_ms = 0;
            if (state.pressure_started_at_ms == 0) {
                state.pressure_started_at_ms = now_ms;
            }
        } else if (watch_threshold != 0 &&
                   state.pressure_value_ewma >= watch_threshold) {
            state.level = NetPressureLevel::kWatch;
            state.consecutive_constrained_samples = 0;
            ++state.consecutive_watch_samples;
            state.consecutive_normal_samples = 0;
            state.normal_since_ms = 0;
            if (state.pressure_started_at_ms == 0) {
                state.pressure_started_at_ms = now_ms;
            }
        } else {
            state.level = NetPressureLevel::kNormal;
            state.consecutive_constrained_samples = 0;
            state.consecutive_watch_samples = 0;
            ++state.consecutive_normal_samples;
            if (previous_level != NetPressureLevel::kNormal) {
                state.normal_since_ms = now_ms;
            }
            if (state.consecutive_normal_samples >=
                options_.recovery_sample_threshold) {
                state.pressure_started_at_ms = 0;
            }
        }
        return &state;
    }

    void MaybeUpdateClearedTarget(const std::string &key_prefix,
                                  const std::string &protocol,
                                  const std::string &target,
                                  PressureMetric metric,
                                  uint32_t pending_bytes,
                                  int64_t now_ms) {
        const std::string key = TargetKey(key_prefix, protocol, target);
        if (target_states_.find(key) == target_states_.end()) {
            return;
        }
        (void)UpdateTarget(key_prefix, protocol, target, metric, 0,
                           pending_bytes, now_ms);
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

    std::string RecommendationReason(const ObservedTargetState &state,
                                     bool constrained) const {
        switch (state.pressure_signal) {
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
        std::vector<NetRecommendation> *recommendations,
        ObservedTargetState *state,
        NetRecommendationType type,
        int64_t now_ms,
        const std::string &reason) const {
        if (recommendations == nullptr ||
            recommendations->size() >= kMaxRecommendations ||
            state == nullptr) {
            return;
        }
        const bool enough_constrained =
            state->level == NetPressureLevel::kConstrained &&
            state->consecutive_constrained_samples >=
                options_.constrained_sample_threshold;
        if (!enough_constrained) {
            return;
        }
        if (state->last_recommendation_ms != 0 &&
            now_ms - state->last_recommendation_ms <
                static_cast<int64_t>(options_.recommendation_cooldown_ms)) {
            return;
        }
        NetRecommendation recommendation;
        recommendation.type = type;
        recommendation.level = state->level;
        recommendation.protocol = state->protocol;
        recommendation.target = state->target;
        recommendation.reason = reason;
        recommendation.pressure_signal = state->pressure_signal;
        recommendation.pressure_value = state->pressure_value;
        recommendation.pressure_value_ewma = state->pressure_value_ewma;
        recommendation.pending_bytes = state->pending_bytes;
        recommendation.pending_bytes_ewma = state->pending_bytes_ewma;
        recommendation.consecutive_watch_samples =
            state->consecutive_watch_samples;
        recommendation.consecutive_constrained_samples =
            state->consecutive_constrained_samples;
        recommendation.recommended_at_ms = now_ms;
        recommendations->push_back(recommendation);
        state->last_recommendation_ms = now_ms;
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

    void ExpireIdleTargets(int64_t now_ms) {
        for (auto it = target_states_.begin(); it != target_states_.end();) {
            if (now_ms - it->second.last_seen_ms > kTargetIdleExpireMs) {
                it = target_states_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void FillTargetStats(NetStatSnapshot *stats) const {
        if (stats == nullptr) {
            return;
        }
        stats->tracked_targets =
            static_cast<uint32_t>(target_states_.size());
        for (const auto &entry : target_states_) {
            if (entry.second.level == NetPressureLevel::kWatch) {
                ++stats->watch_targets;
            } else if (entry.second.level ==
                       NetPressureLevel::kConstrained) {
                ++stats->constrained_targets;
            }
            if (entry.second.level == NetPressureLevel::kNormal &&
                entry.second.consecutive_normal_samples > 0 &&
                entry.second.normal_since_ms != 0) {
                ++stats->recovering_targets;
            }
        }
    }

    NetPressureTarget ToPressureTarget(
        const ObservedTargetState &source) const {
        NetPressureTarget target;
        target.level = source.level;
        target.protocol = source.protocol;
        target.target = source.target;
        target.pressure_signal = source.pressure_signal;
        target.pressure_value = source.pressure_value;
        target.pressure_value_ewma = source.pressure_value_ewma;
        target.pending_bytes = source.pending_bytes;
        target.pending_bytes_ewma = source.pending_bytes_ewma;
        target.consecutive_watch_samples = source.consecutive_watch_samples;
        target.consecutive_constrained_samples =
            source.consecutive_constrained_samples;
        target.consecutive_normal_samples = source.consecutive_normal_samples;
        target.pressure_started_at_ms = source.pressure_started_at_ms;
        target.normal_since_ms = source.normal_since_ms;
        target.last_seen_ms = source.last_seen_ms;
        target.last_recommendation_ms = source.last_recommendation_ms;
        return target;
    }

    bool BuildPressureChangeEvent(
        NetPressureLevel previous_level,
        const NetStatSnapshot &stats,
        event::Event *pressure_event) {
        if (event_ == nullptr || previous_level == stats.level ||
            last_published_level_ == stats.level ||
            pressure_event == nullptr) {
            return false;
        }
        pressure_event->type = event::EventType::kNetPressureChanged;
        pressure_event->source = kServiceName;
        pressure_event->target = "connections";
        pressure_event->message = PressureLevelReason(stats.level);
        pressure_event->value = static_cast<int32_t>(stats.tracked_targets);
        pressure_event->level = static_cast<uint8_t>(stats.level);
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
    INetEngine *net_engine_ = nullptr;
    event::Dispatcher *event_ = nullptr;
    event::Subscription event_subscription_;
    bool started_ = false;
    bool stopping_ = false;
    ProtocolActivity protocol_activity_;
    NetPressureLevel last_published_level_ = NetPressureLevel::kNormal;
    std::map<std::string, ObservedTargetState> target_states_;
    std::thread sample_thread_;
    std::condition_variable condition_;
    mutable std::mutex mutex_;
    NetStatSnapshot stats_;
    std::vector<NetRecommendation> recommendations_;
    std::vector<NetRecommendation> recommendation_history_;
};

std::unique_ptr<INetStat> CreateNetStat(
    const NetStatOptions &options,
    const NetStatDependencies &dependencies) {
    return std::unique_ptr<INetStat>(
        new NetStatImpl(options, dependencies));
}

const char *NetStat::Name() { return kServiceName; }

}  // namespace live_stream
