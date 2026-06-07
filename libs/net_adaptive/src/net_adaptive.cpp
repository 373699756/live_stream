#include "net_adaptive.h"

#include "infra/log.h"
#include "infra/time.h"
#include "media_source.h"
#include "net.h"
#include "rtsp.h"
#include "webrtc.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
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

NetAdaptivePressureLevel MaxLevel(NetAdaptivePressureLevel left,
                                  NetAdaptivePressureLevel right) {
    return static_cast<int>(left) >= static_cast<int>(right) ? left : right;
}

std::string StreamIdName(StreamId stream_id) {
    return stream_id == StreamId::kSub ? "sub" : "main";
}

struct ObservedTargetState {
    std::string key;
    std::string protocol;
    std::string target;
    StreamId stream_id = StreamId::kMain;
    uint32_t pending_bytes = 0;
    uint32_t pending_bytes_ewma = 0;
    uint32_t consecutive_watch_samples = 0;
    uint32_t consecutive_constrained_samples = 0;
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
          media_source_(dependencies.media_source) {}

    ~NetAdaptiveImpl() override { Stop(); }

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

    void Stop() override {
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
            stopping_ = false;
        }
        started_ = false;
    }

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

private:
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
        SampleMediaSource(now_ms, &next_stats, &next_recommendations);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ExpireIdleTargets(now_ms);
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
            std::lock_guard<std::mutex> lock(mutex_);
            ObservedTargetState *state = UpdateTarget(
                "net",
                connection.owner_protocol,
                connection.remote_address.ip,
                StreamId::kMain,
                connection.pending_bytes,
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
                                  "tcp_pending_bytes_high");
            } else if (state->level == NetAdaptivePressureLevel::kWatch) {
                AddRecommendationIfReady(recommendations,
                                  state,
                                  NetAdaptiveRecommendationType::kPreferSubStream,
                                  now_ms,
                                  "tcp_pending_bytes_watch");
            }
        }
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
                                  "rtsp_pending_bytes_high");
            } else if (state->level == NetAdaptivePressureLevel::kWatch) {
                AddRecommendationIfReady(recommendations,
                                  state,
                                  NetAdaptiveRecommendationType::kPreferSubStream,
                                  now_ms,
                                  "rtsp_pending_bytes_watch");
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
                                   options_.pending_bytes_constrained));
            std::lock_guard<std::mutex> lock(mutex_);
            ObservedTargetState *state = UpdateTarget(
                "webrtc",
                "webrtc",
                "peers",
                StreamId::kMain,
                dropped_delta,
                now_ms);
            if (state != nullptr) {
                stats->level = MaxLevel(stats->level, state->level);
                AddRecommendationIfReady(recommendations,
                              state,
                              NetAdaptiveRecommendationType::kRequestKeyFrame,
                              now_ms,
                              "webrtc_frame_dropped");
            }
        }
        last_webrtc_dropped_frames_ = webrtc_stats.dropped_frames;
    }

    void SampleMediaSource(
        int64_t now_ms,
        NetAdaptiveStats *stats,
        std::vector<NetAdaptiveRecommendation> *recommendations) {
        if (media_source_ == nullptr || stats == nullptr ||
            recommendations == nullptr) {
            return;
        }
        const MediaSourceStats media_stats = media_source_->GetStats();
        stats->slow_media_readers = media_stats.slow_reader_count;
        if (media_stats.slow_reader_count > 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            ObservedTargetState *state = UpdateTarget(
                "media_source",
                "media_source",
                "readers",
                StreamId::kMain,
                options_.pending_bytes_watch,
                now_ms);
            if (state != nullptr) {
                stats->level = MaxLevel(stats->level, state->level);
                AddRecommendationIfReady(recommendations,
                              state,
                              NetAdaptiveRecommendationType::kRequestKeyFrame,
                              now_ms,
                              "media_reader_slow");
            }
        }
    }

    ObservedTargetState *UpdateTarget(const std::string &key_prefix,
                                      const std::string &protocol,
                                      const std::string &target,
                                      StreamId stream_id,
                                      uint32_t pending_bytes,
                                      int64_t now_ms) {
        const std::string key = key_prefix + ":" + protocol + ":" +
                                target + ":" + StreamIdName(stream_id);
        ObservedTargetState &state = target_states_[key];
        if (state.key.empty()) {
            state.key = key;
            state.protocol = protocol;
            state.target = target;
            state.stream_id = stream_id;
            state.pending_bytes_ewma = pending_bytes;
        } else {
            const uint64_t weighted =
                static_cast<uint64_t>(state.pending_bytes_ewma) *
                    kEwmaNumerator +
                static_cast<uint64_t>(pending_bytes);
            state.pending_bytes_ewma =
                static_cast<uint32_t>(weighted / kEwmaDenominator);
        }
        state.pending_bytes = pending_bytes;
        state.last_seen_ms = now_ms;

        if (state.pending_bytes_ewma >= options_.pending_bytes_constrained) {
            state.level = NetAdaptivePressureLevel::kConstrained;
            ++state.consecutive_constrained_samples;
            ++state.consecutive_watch_samples;
        } else if (state.pending_bytes_ewma >= options_.pending_bytes_watch) {
            state.level = NetAdaptivePressureLevel::kWatch;
            state.consecutive_constrained_samples = 0;
            ++state.consecutive_watch_samples;
        } else {
            state.level = NetAdaptivePressureLevel::kNormal;
            state.consecutive_constrained_samples = 0;
            state.consecutive_watch_samples = 0;
        }
        return &state;
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
        for (const auto &entry : target_states_) {
            if (entry.second.level == NetAdaptivePressureLevel::kWatch) {
                ++stats->watch_targets;
            } else if (entry.second.level ==
                       NetAdaptivePressureLevel::kConstrained) {
                ++stats->constrained_targets;
            }
        }
    }

    NetAdaptiveTargetState ToPublicTargetState(
        const ObservedTargetState &source) const {
        NetAdaptiveTargetState target;
        target.level = source.level;
        target.protocol = source.protocol;
        target.target = source.target;
        target.stream_id = source.stream_id;
        target.pending_bytes = source.pending_bytes;
        target.pending_bytes_ewma = source.pending_bytes_ewma;
        target.consecutive_watch_samples = source.consecutive_watch_samples;
        target.consecutive_constrained_samples =
            source.consecutive_constrained_samples;
        target.last_seen_ms = source.last_seen_ms;
        target.last_recommendation_ms = source.last_recommendation_ms;
        return target;
    }

    NetAdaptiveOptions options_;
    NetEngine *net_engine_ = nullptr;
    IRtsp *rtsp_ = nullptr;
    IWebrtc *webrtc_ = nullptr;
    IMediaSource *media_source_ = nullptr;
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
};

std::unique_ptr<INetAdaptive> CreateNetAdaptive(
    const NetAdaptiveOptions &options,
    const NetAdaptiveDependencies &dependencies) {
    return std::unique_ptr<INetAdaptive>(
        new NetAdaptiveImpl(options, dependencies));
}

const char *NetAdaptive::Name() { return kServiceName; }

}  // namespace live_stream
