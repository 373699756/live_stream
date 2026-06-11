#include "net_stat.h"

#include "infra/log.h"
#include "infra/time.h"
#include "net.h"
#include "rtsp.h"
#include "webrtc.h"

#include <algorithm>
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
    kSlowSubscriptions,
    kWebrtcDroppedFrames,
};

NetPressureLevel MaxLevel(NetPressureLevel left, NetPressureLevel right) {
    return static_cast<int>(left) >= static_cast<int>(right) ? left : right;
}

std::string StreamIdName(StreamId stream_id) {
    return stream_id == StreamId::kSub ? "sub" : "main";
}

std::string StreamPressureKey(StreamId stream_id) {
    return StreamIdName(stream_id);
}

std::string TargetKey(const std::string &key_prefix,
                      const std::string &protocol,
                      const std::string &target,
                      StreamId stream_id) {
    return key_prefix + ":" + protocol + ":" + target + ":" +
           StreamIdName(stream_id);
}

NetPressureSignal SignalForMetric(PressureMetric metric) {
    switch (metric) {
        case PressureMetric::kPendingBytes:
            return NetPressureSignal::kTcpPendingBytes;
        case PressureMetric::kSendQueue:
            return NetPressureSignal::kSendQueue;
        case PressureMetric::kSlowSubscriptions:
            return NetPressureSignal::kMediaSlowSubscription;
        case PressureMetric::kWebrtcDroppedFrames:
            return NetPressureSignal::kWebrtcDroppedFrames;
    }
    return NetPressureSignal::kNone;
}

struct ObservedTargetState {
    std::string key;
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
    NetPressureLevel level = NetPressureLevel::kNormal;
};

}  // namespace

class NetStatImpl final : public INetStat {
public:
    NetStatImpl(NetStatOptions options, NetStatDependencies dependencies)
        : options_(std::move(options)),
          net_engine_(dependencies.net_engine),
          rtsp_(dependencies.rtsp),
          webrtc_(dependencies.webrtc),
          media_streams_(dependencies.media_streams) {}

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

    std::vector<NetRecommendation>
    GetRecommendations() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return recommendations_;
    }

    std::vector<NetRecommendation>
    GetRecommendationHistory() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return recommendation_history_;
    }

    std::vector<NetPressureTarget>
    GetPressureTargets() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NetPressureTarget> pressure_targets;
        pressure_targets.reserve(target_states_.size());
        for (const auto &entry : target_states_) {
            pressure_targets.push_back(ToPressureTarget(entry.second));
        }
        return pressure_targets;
    }

    std::vector<NetStreamPressure>
    GetStreamPressures() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NetStreamPressure> pressures;
        pressures.reserve(stream_pressures_.size());
        for (const auto &entry : stream_pressures_) {
            pressures.push_back(entry.second);
        }
        return pressures;
    }

private:
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
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_ = NetStatSnapshot{};
            recommendations_.clear();
            recommendation_history_.clear();
            stream_pressures_.clear();
            stopping_ = false;
        }
        started_ = false;
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
        SampleRtsp(now_ms, &next_stats, &next_recommendations);
        SampleWebrtc(now_ms, &next_stats, &next_recommendations);
        SampleMediaStreams(now_ms, &next_stats, &next_recommendations);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ExpireIdleTargets(now_ms);
            RebuildStreamPressures(now_ms, &next_stats);
            FillTargetStats(&next_stats);
            next_stats.samples = stats_.samples + 1;
            stats_ = next_stats;
            recommendations_ = next_recommendations;
            AppendRecommendationHistory(next_recommendations);
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
                AddRecommendationIfReady(recommendations,
                                         state,
                                         NetRecommendationType::kCloseSlowClient,
                                         now_ms,
                                         RecommendationReason(*state, true));
            } else if (state->level == NetPressureLevel::kWatch) {
                AddRecommendationIfReady(recommendations,
                                         state,
                                         NetRecommendationType::kPreferSubStream,
                                         now_ms,
                                         RecommendationReason(*state, false));
            }
        }
    }

    bool ShouldSkipNetConnection(
        const NetConnectionInfo &connection) const {
        if (connection.owner_protocol.empty()) {
            return true;
        }
        if (connection.owner_protocol == "onvif") {
            return true;
        }
        return connection.owner_protocol == "rtsp" && rtsp_ != nullptr;
    }

    void SampleRtsp(int64_t now_ms,
                    NetStatSnapshot *stats,
                    std::vector<NetRecommendation> *recommendations) {
        if (rtsp_ == nullptr || stats == nullptr ||
            recommendations == nullptr) {
            return;
        }
        const RtspStats rtsp_stats = rtsp_->GetStats();
        stats->active_rtsp_sessions = rtsp_stats.active_sessions;

        const std::vector<RtspSessionInfo> sessions =
            rtsp_->ListSessionInfo();
        for (const RtspSessionInfo &session : sessions) {
            std::lock_guard<std::mutex> lock(mutex_);
            ObservedTargetState *state = UpdateTarget(
                "rtsp",
                "rtsp",
                session.remote_address,
                session.stream_id,
                PressureMetric::kPendingBytes,
                session.pending_bytes,
                session.pending_bytes,
                now_ms);
            if (state == nullptr) {
                continue;
            }
            stats->level = MaxLevel(stats->level, state->level);
            if (state->level == NetPressureLevel::kConstrained) {
                AddRecommendationIfReady(recommendations,
                                         state,
                                         NetRecommendationType::kCloseSlowClient,
                                         now_ms,
                                         RecommendationReason(*state, true));
            } else if (state->level == NetPressureLevel::kWatch) {
                AddRecommendationIfReady(recommendations,
                                         state,
                                         NetRecommendationType::kPreferSubStream,
                                         now_ms,
                                         RecommendationReason(*state, false));
            }
        }
    }

    void SampleWebrtc(int64_t now_ms,
                      NetStatSnapshot *stats,
                      std::vector<NetRecommendation> *recommendations) {
        if (webrtc_ == nullptr || stats == nullptr ||
            recommendations == nullptr) {
            return;
        }
        const WebrtcStats webrtc_stats = webrtc_->GetStats();
        stats->active_webrtc_peers = webrtc_stats.active_peers;
        if (webrtc_stats.dropped_frames > last_webrtc_dropped_frames_) {
            const uint32_t dropped_delta = static_cast<uint32_t>(
                std::min<uint64_t>(webrtc_stats.dropped_frames -
                                       last_webrtc_dropped_frames_,
                                   UINT32_MAX));
            std::lock_guard<std::mutex> lock(mutex_);
            ObservedTargetState *state = UpdateTarget(
                "webrtc",
                "webrtc",
                "peers",
                StreamId::kMain,
                PressureMetric::kWebrtcDroppedFrames,
                dropped_delta,
                0,
                now_ms);
            if (state != nullptr) {
                stats->level = MaxLevel(stats->level, state->level);
                AddRecommendationIfReady(recommendations,
                                         state,
                                         NetRecommendationType::kRequestKeyframe,
                                         now_ms,
                                         RecommendationReason(*state, false));
            }
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            MaybeUpdateClearedTarget("webrtc",
                                     "webrtc",
                                     "peers",
                                     StreamId::kMain,
                                     PressureMetric::kWebrtcDroppedFrames,
                                     0,
                                     now_ms);
        }
        last_webrtc_dropped_frames_ = webrtc_stats.dropped_frames;
    }

    void SampleMediaStreams(
        int64_t now_ms,
        NetStatSnapshot *stats,
        std::vector<NetRecommendation> *recommendations) {
        if (media_streams_ == nullptr || stats == nullptr ||
            recommendations == nullptr) {
            return;
        }
        const MediaStreamStats media_stats =
            media_streams_->GetStreamStats();
        stats->slow_media_subscriptions = media_stats.slow_subscriptions;
        SampleMediaStream(now_ms, StreamId::kMain,
                          media_stats.main_slow_subscriptions, stats,
                          recommendations);
        SampleMediaStream(now_ms, StreamId::kSub,
                          media_stats.sub_slow_subscriptions, stats,
                          recommendations);
    }

    void SampleMediaStream(
        int64_t now_ms,
        StreamId stream_id,
        uint32_t slow_subscriptions,
        NetStatSnapshot *stats,
        std::vector<NetRecommendation> *recommendations) {
        if (stats == nullptr || recommendations == nullptr) {
            return;
        }
        if (slow_subscriptions > 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            ObservedTargetState *state = UpdateTarget(
                "media",
                "media",
                "subscriptions",
                stream_id,
                PressureMetric::kSlowSubscriptions,
                slow_subscriptions,
                0,
                now_ms);
            if (state == nullptr) {
                return;
            }
            stats->level = MaxLevel(stats->level, state->level);
            AddRecommendationIfReady(recommendations, state,
                                     NetRecommendationType::kRequestKeyframe,
                                     now_ms,
                                     RecommendationReason(*state, false));
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        MaybeUpdateClearedTarget("media",
                                 "media",
                                 "subscriptions",
                                 stream_id,
                                 PressureMetric::kSlowSubscriptions,
                                 0,
                                 now_ms);
    }

    ObservedTargetState *UpdateConnectionTarget(
        const NetConnectionInfo &connection,
        int64_t now_ms) {
        ObservedTargetState *pending_state = UpdateTarget(
            "net",
            connection.owner_protocol,
            connection.remote_address.ip,
            StreamId::kMain,
            PressureMetric::kPendingBytes,
            connection.pending_bytes,
            connection.pending_bytes,
            now_ms);
        if (connection.send_queue_length == 0) {
            MaybeUpdateClearedTarget("net_queue",
                                     connection.owner_protocol,
                                     connection.remote_address.ip,
                                     StreamId::kMain,
                                     PressureMetric::kSendQueue,
                                     connection.pending_bytes,
                                     now_ms);
            return pending_state;
        }
        ObservedTargetState *queue_state = UpdateTarget(
            "net_queue",
            connection.owner_protocol,
            connection.remote_address.ip,
            StreamId::kMain,
            PressureMetric::kSendQueue,
            connection.send_queue_length,
            connection.pending_bytes,
            now_ms);
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

    ObservedTargetState *UpdateTarget(const std::string &key_prefix,
                                      const std::string &protocol,
                                      const std::string &target,
                                      StreamId stream_id,
                                      PressureMetric metric,
                                      uint32_t pressure_value,
                                      uint32_t pending_bytes,
                                      int64_t now_ms) {
        const std::string key = TargetKey(key_prefix, protocol, target,
                                          stream_id);
        ObservedTargetState &state = target_states_[key];
        if (state.key.empty()) {
            state.key = key;
            state.protocol = protocol;
            state.target = target;
            state.stream_id = stream_id;
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
                                  StreamId stream_id,
                                  PressureMetric metric,
                                  uint32_t pending_bytes,
                                  int64_t now_ms) {
        const std::string key = TargetKey(key_prefix, protocol, target,
                                          stream_id);
        if (target_states_.find(key) == target_states_.end()) {
            return;
        }
        (void)UpdateTarget(key_prefix, protocol, target, stream_id, metric, 0,
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
            case PressureMetric::kSlowSubscriptions:
                return options_.slow_media_subscriptions_watch;
            case PressureMetric::kWebrtcDroppedFrames:
                return options_.webrtc_dropped_frames_watch;
        }
        return 0;
    }

    uint32_t ConstrainedThreshold(PressureMetric metric) const {
        switch (metric) {
            case PressureMetric::kPendingBytes:
                return options_.pending_bytes_constrained;
            case PressureMetric::kSendQueue:
                return options_.send_queue_constrained;
            case PressureMetric::kSlowSubscriptions:
                return options_.slow_media_subscriptions_constrained;
            case PressureMetric::kWebrtcDroppedFrames:
                return options_.webrtc_dropped_frames_constrained;
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
            case NetPressureSignal::kMediaSlowSubscription:
                return constrained ? "media_subscriptions_constrained"
                                   : "media_subscription_slow";
            case NetPressureSignal::kWebrtcDroppedFrames:
                return constrained ? "webrtc_frames_dropped_high"
                                   : "webrtc_frame_dropped";
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
        const bool enough_watch =
            state->level == NetPressureLevel::kWatch &&
            state->consecutive_watch_samples >= options_.watch_sample_threshold;
        if (!enough_constrained && !enough_watch) {
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
        recommendation.stream_id = state->stream_id;
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
        stats->pressure_streams =
            static_cast<uint32_t>(stream_pressures_.size());
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

    void RebuildStreamPressures(int64_t now_ms,
                                NetStatSnapshot *stats) {
        std::map<std::string, NetStreamPressure> next_pressures;
        for (const auto &entry : target_states_) {
            const ObservedTargetState &target = entry.second;
            NetStreamPressure &stream_pressure =
                StreamPressureForTarget(&next_pressures, target, now_ms);
            AccumulateStreamPressure(&stream_pressure, target);
        }

        for (auto &entry : next_pressures) {
            FinalizeStreamPressure(&entry.second, stats);
        }
        stream_pressures_ = std::move(next_pressures);
    }

    NetStreamPressure &StreamPressureForTarget(
        std::map<std::string, NetStreamPressure> *pressures,
        const ObservedTargetState &target,
        int64_t now_ms) const {
        NetStreamPressure &stream_pressure =
            (*pressures)[StreamPressureKey(target.stream_id)];
        if (stream_pressure.tracked_targets == 0) {
            stream_pressure.stream_id = target.stream_id;
            stream_pressure.updated_at_ms = now_ms;
            stream_pressure.normal_since_ms = now_ms;
        }
        return stream_pressure;
    }

    void AccumulateStreamPressure(
        NetStreamPressure *stream_pressure,
        const ObservedTargetState &target) const {
        ++stream_pressure->tracked_targets;
        AccumulateStreamPressureLevel(stream_pressure, target);
        AccumulateStreamPressurePeaks(stream_pressure, target);
        AccumulateStreamPressureSignal(stream_pressure, target);
        AccumulateStreamPressureTiming(stream_pressure, target);
    }

    void AccumulateStreamPressureLevel(
        NetStreamPressure *stream_pressure,
        const ObservedTargetState &target) const {
        stream_pressure->level =
            MaxLevel(stream_pressure->level, target.level);
        if (target.level == NetPressureLevel::kWatch) {
            ++stream_pressure->watch_targets;
        } else if (target.level == NetPressureLevel::kConstrained) {
            ++stream_pressure->constrained_targets;
        }
    }

    void AccumulateStreamPressurePeaks(
        NetStreamPressure *stream_pressure,
        const ObservedTargetState &target) const {
        stream_pressure->peak_pending_bytes_ewma =
            std::max(stream_pressure->peak_pending_bytes_ewma,
                     target.pending_bytes_ewma);
        stream_pressure->peak_pressure_value_ewma =
            std::max(stream_pressure->peak_pressure_value_ewma,
                     target.pressure_value_ewma);
    }

    void AccumulateStreamPressureSignal(
        NetStreamPressure *stream_pressure,
        const ObservedTargetState &target) const {
        if (target.pressure_signal ==
            NetPressureSignal::kMediaSlowSubscription) {
            stream_pressure->slow_media_subscriptions +=
                target.pressure_value;
        } else if (target.pressure_signal ==
                   NetPressureSignal::kWebrtcDroppedFrames) {
            stream_pressure->webrtc_dropped_frames_delta +=
                target.pressure_value;
        }
    }

    void AccumulateStreamPressureTiming(
        NetStreamPressure *stream_pressure,
        const ObservedTargetState &target) const {
        if (IsEarlierPressureStart(*stream_pressure, target)) {
            stream_pressure->pressure_started_at_ms =
                target.pressure_started_at_ms;
        }
        if (!TargetCanRestoreStream(target)) {
            stream_pressure->normal_since_ms = 0;
        } else if (stream_pressure->normal_since_ms != 0 &&
                   target.normal_since_ms >
                       stream_pressure->normal_since_ms) {
            stream_pressure->normal_since_ms = target.normal_since_ms;
        }
    }

    bool IsEarlierPressureStart(
        const NetStreamPressure &stream_pressure,
        const ObservedTargetState &target) const {
        return target.pressure_started_at_ms != 0 &&
               (stream_pressure.pressure_started_at_ms == 0 ||
                target.pressure_started_at_ms <
                    stream_pressure.pressure_started_at_ms);
    }

    bool TargetCanRestoreStream(
        const ObservedTargetState &target) const {
        return target.normal_since_ms != 0 &&
               target.consecutive_normal_samples >=
                   options_.recovery_sample_threshold;
    }

    void FinalizeStreamPressure(NetStreamPressure *stream_pressure,
                                NetStatSnapshot *stats) const {
        stream_pressure->need_keyframe =
            stream_pressure->slow_media_subscriptions > 0 ||
            stream_pressure->webrtc_dropped_frames_delta > 0 ||
            stream_pressure->level != NetPressureLevel::kNormal;
        stream_pressure->prefer_sub_stream =
            stream_pressure->stream_id == StreamId::kMain &&
            stream_pressure->level != NetPressureLevel::kNormal;
        stream_pressure->close_slow_clients =
            stream_pressure->constrained_targets > 0;
        stream_pressure->can_restore_main_stream =
            stream_pressure->stream_id == StreamId::kMain &&
            stream_pressure->level == NetPressureLevel::kNormal &&
            stream_pressure->tracked_targets > 0 &&
            stream_pressure->normal_since_ms != 0;
        stream_pressure->reason = StreamPressureReason(*stream_pressure);
        UpdateStatsFromStreamPressure(*stream_pressure, stats);
    }

    void UpdateStatsFromStreamPressure(
        const NetStreamPressure &stream_pressure,
        NetStatSnapshot *stats) const {
        if (stats == nullptr) {
            return;
        }
        stats->level = MaxLevel(stats->level, stream_pressure.level);
        if (stream_pressure.can_restore_main_stream) {
            ++stats->recovering_streams;
        }
    }

    std::string StreamPressureReason(
        const NetStreamPressure &stream_pressure) const {
        if (stream_pressure.constrained_targets > 0) {
            return "stream_constrained_targets";
        }
        if (stream_pressure.watch_targets > 0) {
            return "stream_watch_targets";
        }
        if (stream_pressure.webrtc_dropped_frames_delta > 0) {
            return "stream_webrtc_drops";
        }
        if (stream_pressure.slow_media_subscriptions > 0) {
            return "stream_slow_subscriptions";
        }
        if (stream_pressure.can_restore_main_stream) {
            return "stream_recovered";
        }
        return "stream_normal";
    }

    NetPressureTarget ToPressureTarget(
        const ObservedTargetState &source) const {
        NetPressureTarget target;
        target.level = source.level;
        target.protocol = source.protocol;
        target.target = source.target;
        target.stream_id = source.stream_id;
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

    NetStatOptions options_;
    INetEngine *net_engine_ = nullptr;
    IRtsp *rtsp_ = nullptr;
    IWebrtc *webrtc_ = nullptr;
    MediaStreams *media_streams_ = nullptr;
    bool started_ = false;
    bool stopping_ = false;
    uint64_t last_webrtc_dropped_frames_ = 0;
    std::map<std::string, ObservedTargetState> target_states_;
    std::thread sample_thread_;
    std::condition_variable condition_;
    mutable std::mutex mutex_;
    NetStatSnapshot stats_;
    std::vector<NetRecommendation> recommendations_;
    std::vector<NetRecommendation> recommendation_history_;
    std::map<std::string, NetStreamPressure> stream_pressures_;
};

std::unique_ptr<INetStat> CreateNetStat(
    const NetStatOptions &options,
    const NetStatDependencies &dependencies) {
    return std::unique_ptr<INetStat>(
        new NetStatImpl(options, dependencies));
}

const char *NetStat::Name() { return kServiceName; }

}  // namespace live_stream
