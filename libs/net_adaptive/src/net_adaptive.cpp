#include "net_adaptive.h"

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

constexpr const char *kServiceName = "net_adaptive";
constexpr size_t kMaxRecommendations = 16;
constexpr uint32_t kEwmaNumerator = 3;
constexpr uint32_t kEwmaDenominator = 4;
constexpr int64_t kTargetIdleExpireMs = 30000;

enum class PressureMetric {
    kPendingBytes,
    kSendQueue,
    kSlowReaders,
    kWebrtcDroppedFrames,
};

NetAdaptivePressureLevel MaxLevel(NetAdaptivePressureLevel left,
                                  NetAdaptivePressureLevel right) {
    return static_cast<int>(left) >= static_cast<int>(right) ? left : right;
}

std::string StreamIdName(StreamId stream_id) {
    return stream_id == StreamId::kSub ? "sub" : "main";
}

std::string StreamDecisionKey(StreamId stream_id) {
    return StreamIdName(stream_id);
}

std::string TargetKey(const std::string &key_prefix,
                      const std::string &protocol,
                      const std::string &target,
                      StreamId stream_id) {
    return key_prefix + ":" + protocol + ":" + target + ":" +
           StreamIdName(stream_id);
}

NetAdaptivePressureSignal SignalForMetric(PressureMetric metric) {
    switch (metric) {
        case PressureMetric::kPendingBytes:
            return NetAdaptivePressureSignal::kTcpPendingBytes;
        case PressureMetric::kSendQueue:
            return NetAdaptivePressureSignal::kSendQueue;
        case PressureMetric::kSlowReaders:
            return NetAdaptivePressureSignal::kMediaSlowReader;
        case PressureMetric::kWebrtcDroppedFrames:
            return NetAdaptivePressureSignal::kWebrtcDroppedFrames;
    }
    return NetAdaptivePressureSignal::kNone;
}

struct ObservedTargetState {
    std::string key;
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
    NetAdaptivePressureLevel level = NetAdaptivePressureLevel::kNormal;
};

}  // namespace

class NetAdaptiveImpl final : public INetAdaptive {
public:
    NetAdaptiveImpl(NetAdaptiveOptions options,
                    NetAdaptiveDependencies dependencies)
        : options_(std::move(options)),
          net_engine_(dependencies.net_engine),
          rtsp_(dependencies.rtsp),
          webrtc_(dependencies.webrtc),
          media_streams_(dependencies.media_streams) {}

    ~NetAdaptiveImpl() override { StopInternal(); }

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
        sample_thread_ = std::thread(&NetAdaptiveImpl::SampleLoop, this);
        started_ = true;
        Info(kServiceName, "started interval_ms=%u",
             static_cast<unsigned>(options_.sample_interval_ms));
        return true;
    }

    void Stop() override { StopInternal(); }

    NetAdaptiveStats GetStats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    std::vector<NetAdaptiveRecommendation>
    GetRecommendations() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return recommendations_;
    }

    std::vector<NetAdaptiveRecommendation>
    GetRecommendationHistory() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return recommendation_history_;
    }

    std::vector<NetAdaptiveTargetState>
    GetTargetStates() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NetAdaptiveTargetState> states;
        states.reserve(target_states_.size());
        for (const auto &entry : target_states_) {
            states.push_back(ToPublicTargetState(entry.second));
        }
        return states;
    }

    std::vector<NetAdaptiveStreamDecision>
    GetStreamDecisions() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NetAdaptiveStreamDecision> decisions;
        decisions.reserve(stream_decisions_.size());
        for (const auto &entry : stream_decisions_) {
            decisions.push_back(entry.second);
        }
        return decisions;
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
            stats_ = NetAdaptiveStats{};
            recommendations_.clear();
            recommendation_history_.clear();
            stream_decisions_.clear();
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
        NetAdaptiveStats next_stats;
        std::vector<NetAdaptiveRecommendation> next_recommendations;
        const int64_t now_ms = infra::Time::MonotonicMillis();
        next_stats.enabled = options_.enabled;

        SampleNet(now_ms, &next_stats, &next_recommendations);
        SampleRtsp(now_ms, &next_stats, &next_recommendations);
        SampleWebrtc(now_ms, &next_stats, &next_recommendations);
        SampleMediaStreams(now_ms, &next_stats, &next_recommendations);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ExpireIdleTargets(now_ms);
            RebuildStreamDecisions(now_ms, &next_stats);
            FillTargetStats(&next_stats);
            next_stats.samples = stats_.samples + 1;
            stats_ = next_stats;
            recommendations_ = next_recommendations;
            AppendRecommendationHistory(next_recommendations);
        }
    }

    void SampleNet(int64_t now_ms,
                   NetAdaptiveStats *stats,
                   std::vector<NetAdaptiveRecommendation> *recommendations) {
        if (net_engine_ == nullptr || stats == nullptr ||
            recommendations == nullptr) {
            return;
        }
        const std::vector<NetConnectionDiagnostics> connections =
            net_engine_->GetConnectionDiagnosticsSnapshot();
        for (const NetConnectionDiagnostics &connection : connections) {
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
            if (state->level == NetAdaptivePressureLevel::kConstrained) {
                ++stats->constrained_connections;
                AddRecommendationIfReady(recommendations,
                                  state,
                                  NetAdaptiveRecommendationType::kCloseSlowClient,
                                  now_ms,
                                  RecommendationReason(*state, true));
            } else if (state->level == NetAdaptivePressureLevel::kWatch) {
                AddRecommendationIfReady(recommendations,
                                  state,
                                  NetAdaptiveRecommendationType::kPreferSubStream,
                                  now_ms,
                                  RecommendationReason(*state, false));
            }
        }
    }

    bool ShouldSkipNetConnection(
        const NetConnectionDiagnostics &connection) const {
        if (connection.owner_protocol.empty()) {
            return true;
        }
        if (connection.owner_protocol == "onvif") {
            return true;
        }
        return connection.owner_protocol == "rtsp" && rtsp_ != nullptr;
    }

    void SampleRtsp(int64_t now_ms,
                    NetAdaptiveStats *stats,
                    std::vector<NetAdaptiveRecommendation> *recommendations) {
        if (rtsp_ == nullptr || stats == nullptr ||
            recommendations == nullptr) {
            return;
        }
        const RtspStats rtsp_stats = rtsp_->GetStats();
        stats->active_rtsp_sessions = rtsp_stats.active_sessions;

        const std::vector<RtspSessionDiagnostics> sessions =
            rtsp_->GetSessionDiagnostics();
        for (const RtspSessionDiagnostics &session : sessions) {
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
            if (state->level == NetAdaptivePressureLevel::kConstrained) {
                AddRecommendationIfReady(recommendations,
                                  state,
                                  NetAdaptiveRecommendationType::kCloseSlowClient,
                                  now_ms,
                                  RecommendationReason(*state, true));
            } else if (state->level == NetAdaptivePressureLevel::kWatch) {
                AddRecommendationIfReady(recommendations,
                                  state,
                                  NetAdaptiveRecommendationType::kPreferSubStream,
                                  now_ms,
                                  RecommendationReason(*state, false));
            }
        }
    }

    void SampleWebrtc(int64_t now_ms,
                      NetAdaptiveStats *stats,
                      std::vector<NetAdaptiveRecommendation> *recommendations) {
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
                              NetAdaptiveRecommendationType::kRequestKeyFrame,
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
        NetAdaptiveStats *stats,
        std::vector<NetAdaptiveRecommendation> *recommendations) {
        if (media_streams_ == nullptr || stats == nullptr ||
            recommendations == nullptr) {
            return;
        }
        const MediaStreamCounters media_counters =
            media_streams_->GetStreamCounters();
        stats->slow_media_readers = media_counters.slow_subscriber_count;
        SampleMediaStream(now_ms, StreamId::kMain,
                          media_counters.main_slow_subscriber_count, stats,
                          recommendations);
        SampleMediaStream(now_ms, StreamId::kSub,
                          media_counters.sub_slow_subscriber_count, stats,
                          recommendations);
    }

    void SampleMediaStream(
        int64_t now_ms,
        StreamId stream_id,
        uint32_t slow_subscriber_count,
        NetAdaptiveStats *stats,
        std::vector<NetAdaptiveRecommendation> *recommendations) {
        if (stats == nullptr || recommendations == nullptr) {
            return;
        }
        if (slow_subscriber_count > 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            ObservedTargetState *state = UpdateTarget(
                "media",
                "media",
                "subscribers",
                stream_id,
                PressureMetric::kSlowReaders,
                slow_subscriber_count,
                0,
                now_ms);
            if (state == nullptr) {
                return;
            }
            stats->level = MaxLevel(stats->level, state->level);
            AddRecommendationIfReady(recommendations,
                          state,
                          NetAdaptiveRecommendationType::kRequestKeyFrame,
                          now_ms,
                          RecommendationReason(*state, false));
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        MaybeUpdateClearedTarget("media",
                                 "media",
                                 "subscribers",
                                 stream_id,
                                 PressureMetric::kSlowReaders,
                                 0,
                                 now_ms);
    }

    ObservedTargetState *UpdateConnectionTarget(
        const NetConnectionDiagnostics &connection,
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
        const NetAdaptivePressureLevel previous_level = state.level;
        if (constrained_threshold != 0 &&
            state.pressure_value_ewma >= constrained_threshold) {
            state.level = NetAdaptivePressureLevel::kConstrained;
            ++state.consecutive_constrained_samples;
            ++state.consecutive_watch_samples;
            state.consecutive_normal_samples = 0;
            state.normal_since_ms = 0;
            if (state.pressure_started_at_ms == 0) {
                state.pressure_started_at_ms = now_ms;
            }
        } else if (watch_threshold != 0 &&
                   state.pressure_value_ewma >= watch_threshold) {
            state.level = NetAdaptivePressureLevel::kWatch;
            state.consecutive_constrained_samples = 0;
            ++state.consecutive_watch_samples;
            state.consecutive_normal_samples = 0;
            state.normal_since_ms = 0;
            if (state.pressure_started_at_ms == 0) {
                state.pressure_started_at_ms = now_ms;
            }
        } else {
            state.level = NetAdaptivePressureLevel::kNormal;
            state.consecutive_constrained_samples = 0;
            state.consecutive_watch_samples = 0;
            ++state.consecutive_normal_samples;
            if (previous_level != NetAdaptivePressureLevel::kNormal) {
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
            case PressureMetric::kSlowReaders:
                return options_.slow_readers_watch;
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
            case PressureMetric::kSlowReaders:
                return options_.slow_readers_constrained;
            case PressureMetric::kWebrtcDroppedFrames:
                return options_.webrtc_dropped_frames_constrained;
        }
        return 0;
    }

    std::string RecommendationReason(const ObservedTargetState &state,
                                     bool constrained) const {
        switch (state.pressure_signal) {
            case NetAdaptivePressureSignal::kTcpPendingBytes:
                return constrained ? "tcp_pending_bytes_high"
                                   : "tcp_pending_bytes_watch";
            case NetAdaptivePressureSignal::kSendQueue:
                return constrained ? "tcp_send_queue_high"
                                   : "tcp_send_queue_watch";
            case NetAdaptivePressureSignal::kMediaSlowReader:
                return constrained ? "media_readers_constrained"
                                   : "media_reader_slow";
            case NetAdaptivePressureSignal::kWebrtcDroppedFrames:
                return constrained ? "webrtc_frames_dropped_high"
                                   : "webrtc_frame_dropped";
            case NetAdaptivePressureSignal::kNone:
                break;
        }
        return "pressure_observed";
    }

    void AddRecommendationIfReady(
        std::vector<NetAdaptiveRecommendation> *recommendations,
        ObservedTargetState *state,
        NetAdaptiveRecommendationType type,
        int64_t now_ms,
        const std::string &reason) const {
        if (recommendations == nullptr ||
            recommendations->size() >= kMaxRecommendations ||
            state == nullptr) {
            return;
        }
        const bool enough_constrained =
            state->level == NetAdaptivePressureLevel::kConstrained &&
            state->consecutive_constrained_samples >=
                options_.constrained_sample_threshold;
        const bool enough_watch =
            state->level == NetAdaptivePressureLevel::kWatch &&
            state->consecutive_watch_samples >= options_.watch_sample_threshold;
        if (!enough_constrained && !enough_watch) {
            return;
        }
        if (state->last_recommendation_ms != 0 &&
            now_ms - state->last_recommendation_ms <
                static_cast<int64_t>(options_.recommendation_cooldown_ms)) {
            return;
        }
        NetAdaptiveRecommendation recommendation;
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
        const std::vector<NetAdaptiveRecommendation> &recommendations) {
        if (recommendations.empty() ||
            options_.recommendation_history_limit == 0) {
            return;
        }
        for (const NetAdaptiveRecommendation &recommendation :
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

    void FillTargetStats(NetAdaptiveStats *stats) const {
        if (stats == nullptr) {
            return;
        }
        stats->tracked_targets =
            static_cast<uint32_t>(target_states_.size());
        stats->stream_decisions =
            static_cast<uint32_t>(stream_decisions_.size());
        for (const auto &entry : target_states_) {
            if (entry.second.level == NetAdaptivePressureLevel::kWatch) {
                ++stats->watch_targets;
            } else if (entry.second.level ==
                       NetAdaptivePressureLevel::kConstrained) {
                ++stats->constrained_targets;
            }
            if (entry.second.level == NetAdaptivePressureLevel::kNormal &&
                entry.second.consecutive_normal_samples > 0 &&
                entry.second.normal_since_ms != 0) {
                ++stats->recovering_targets;
            }
        }
    }

    void RebuildStreamDecisions(int64_t now_ms,
                                NetAdaptiveStats *stats) {
        std::map<std::string, NetAdaptiveStreamDecision> next_decisions;
        for (const auto &entry : target_states_) {
            const ObservedTargetState &target = entry.second;
            NetAdaptiveStreamDecision &decision =
                next_decisions[StreamDecisionKey(target.stream_id)];
            if (decision.tracked_targets == 0) {
                decision.stream_id = target.stream_id;
                decision.updated_at_ms = now_ms;
                decision.normal_since_ms = now_ms;
            }
            ++decision.tracked_targets;
            decision.level = MaxLevel(decision.level, target.level);
            if (target.level == NetAdaptivePressureLevel::kWatch) {
                ++decision.watch_targets;
            } else if (target.level == NetAdaptivePressureLevel::kConstrained) {
                ++decision.constrained_targets;
            }
            decision.peak_pending_bytes_ewma =
                std::max(decision.peak_pending_bytes_ewma,
                         target.pending_bytes_ewma);
            decision.peak_pressure_value_ewma =
                std::max(decision.peak_pressure_value_ewma,
                         target.pressure_value_ewma);
            if (target.pressure_signal ==
                NetAdaptivePressureSignal::kMediaSlowReader) {
                decision.slow_media_readers += target.pressure_value;
            } else if (target.pressure_signal ==
                       NetAdaptivePressureSignal::kWebrtcDroppedFrames) {
                decision.webrtc_dropped_frames_delta += target.pressure_value;
            }
            if (target.pressure_started_at_ms != 0 &&
                (decision.pressure_started_at_ms == 0 ||
                 target.pressure_started_at_ms <
                     decision.pressure_started_at_ms)) {
                decision.pressure_started_at_ms =
                    target.pressure_started_at_ms;
            }
            if (target.normal_since_ms == 0 ||
                target.consecutive_normal_samples <
                    options_.recovery_sample_threshold) {
                decision.normal_since_ms = 0;
            } else if (decision.normal_since_ms != 0 &&
                       target.normal_since_ms > decision.normal_since_ms) {
                decision.normal_since_ms = target.normal_since_ms;
            }
        }

        for (auto &entry : next_decisions) {
            NetAdaptiveStreamDecision &decision = entry.second;
            decision.should_request_key_frame =
                decision.slow_media_readers > 0 ||
                decision.webrtc_dropped_frames_delta > 0 ||
                decision.level != NetAdaptivePressureLevel::kNormal;
            decision.should_prefer_sub_stream =
                decision.stream_id == StreamId::kMain &&
                decision.level != NetAdaptivePressureLevel::kNormal;
            decision.should_close_slow_clients =
                decision.constrained_targets > 0;
            decision.may_restore_main_stream =
                decision.stream_id == StreamId::kMain &&
                decision.level == NetAdaptivePressureLevel::kNormal &&
                decision.tracked_targets > 0 &&
                decision.normal_since_ms != 0;
            decision.reason = StreamDecisionReason(decision);
            if (stats != nullptr) {
                stats->level = MaxLevel(stats->level, decision.level);
                if (decision.may_restore_main_stream) {
                    ++stats->recovering_streams;
                }
            }
        }
        stream_decisions_ = std::move(next_decisions);
    }

    std::string StreamDecisionReason(
        const NetAdaptiveStreamDecision &decision) const {
        if (decision.constrained_targets > 0) {
            return "stream_constrained_targets";
        }
        if (decision.watch_targets > 0) {
            return "stream_watch_targets";
        }
        if (decision.webrtc_dropped_frames_delta > 0) {
            return "stream_webrtc_drops";
        }
        if (decision.slow_media_readers > 0) {
            return "stream_slow_readers";
        }
        if (decision.may_restore_main_stream) {
            return "stream_recovered";
        }
        return "stream_normal";
    }

    NetAdaptiveTargetState ToPublicTargetState(
        const ObservedTargetState &source) const {
        NetAdaptiveTargetState target;
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

    NetAdaptiveOptions options_;
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
    NetAdaptiveStats stats_;
    std::vector<NetAdaptiveRecommendation> recommendations_;
    std::vector<NetAdaptiveRecommendation> recommendation_history_;
    std::map<std::string, NetAdaptiveStreamDecision> stream_decisions_;
};

std::unique_ptr<INetAdaptive> CreateNetAdaptive(
    const NetAdaptiveOptions &options,
    const NetAdaptiveDependencies &dependencies) {
    return std::unique_ptr<INetAdaptive>(
        new NetAdaptiveImpl(options, dependencies));
}

const char *NetAdaptive::Name() { return kServiceName; }

}  // namespace live_stream
