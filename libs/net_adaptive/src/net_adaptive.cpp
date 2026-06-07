#include "net_adaptive.h"

#include "infra/log.h"
#include "media_source.h"
#include "net.h"
#include "rtsp.h"
#include "webrtc.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char *kServiceName = "net_adaptive";
constexpr size_t kMaxRecommendations = 16;

NetAdaptivePressureLevel MaxLevel(NetAdaptivePressureLevel left,
                                  NetAdaptivePressureLevel right) {
    return static_cast<int>(left) >= static_cast<int>(right) ? left : right;
}

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
        next_stats.enabled = options_.enabled;

        SampleNet(&next_stats, &next_recommendations);
        SampleRtsp(&next_stats, &next_recommendations);
        SampleWebrtc(&next_stats, &next_recommendations);
        SampleMediaSource(&next_stats, &next_recommendations);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            next_stats.samples = stats_.samples + 1;
            stats_ = next_stats;
            recommendations_ = next_recommendations;
        }
    }

    void SampleNet(NetAdaptiveStats *stats,
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
            const NetAdaptivePressureLevel level =
                ConnectionPressureLevel(connection);
            stats->level = MaxLevel(stats->level, level);
            if (level == NetAdaptivePressureLevel::kConstrained) {
                ++stats->constrained_connections;
                AddRecommendation(recommendations,
                                  NetAdaptiveRecommendationType::kCloseSlowClient,
                                  level,
                                  connection.owner_protocol,
                                  connection.remote_address.ip,
                                  StreamId::kMain,
                                  "tcp_pending_bytes_high");
            } else if (level == NetAdaptivePressureLevel::kWatch) {
                AddRecommendation(recommendations,
                                  NetAdaptiveRecommendationType::kPreferSubStream,
                                  level,
                                  connection.owner_protocol,
                                  connection.remote_address.ip,
                                  StreamId::kMain,
                                  "tcp_pending_bytes_watch");
            }
        }
    }

    void SampleRtsp(NetAdaptiveStats *stats,
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
            if (session.pending_bytes >= options_.pending_bytes_constrained) {
                stats->level =
                    MaxLevel(stats->level,
                             NetAdaptivePressureLevel::kConstrained);
                AddRecommendation(recommendations,
                                  NetAdaptiveRecommendationType::kCloseSlowClient,
                                  NetAdaptivePressureLevel::kConstrained,
                                  "rtsp",
                                  session.remote_address,
                                  session.stream_id,
                                  "rtsp_pending_bytes_high");
            } else if (session.pending_bytes >= options_.pending_bytes_watch) {
                stats->level =
                    MaxLevel(stats->level, NetAdaptivePressureLevel::kWatch);
                AddRecommendation(recommendations,
                                  NetAdaptiveRecommendationType::kPreferSubStream,
                                  NetAdaptivePressureLevel::kWatch,
                                  "rtsp",
                                  session.remote_address,
                                  session.stream_id,
                                  "rtsp_pending_bytes_watch");
            }
        }
    }

    void SampleWebrtc(NetAdaptiveStats *stats,
                      std::vector<NetAdaptiveRecommendation> *recommendations) {
        if (webrtc_ == nullptr || stats == nullptr ||
            recommendations == nullptr) {
            return;
        }
        const WebrtcStats webrtc_stats = webrtc_->GetStats();
        stats->active_webrtc_peers = webrtc_stats.active_peers;
        if (webrtc_stats.dropped_frames > last_webrtc_dropped_frames_) {
            stats->level = MaxLevel(stats->level, NetAdaptivePressureLevel::kWatch);
            AddRecommendation(recommendations,
                              NetAdaptiveRecommendationType::kRequestKeyFrame,
                              NetAdaptivePressureLevel::kWatch,
                              "webrtc",
                              std::string(),
                              StreamId::kMain,
                              "webrtc_frame_dropped");
        }
        last_webrtc_dropped_frames_ = webrtc_stats.dropped_frames;
    }

    void SampleMediaSource(
        NetAdaptiveStats *stats,
        std::vector<NetAdaptiveRecommendation> *recommendations) {
        if (media_source_ == nullptr || stats == nullptr ||
            recommendations == nullptr) {
            return;
        }
        const MediaSourceStats media_stats = media_source_->GetStats();
        stats->slow_media_readers = media_stats.slow_reader_count;
        if (media_stats.slow_reader_count > 0) {
            stats->level = MaxLevel(stats->level, NetAdaptivePressureLevel::kWatch);
            AddRecommendation(recommendations,
                              NetAdaptiveRecommendationType::kRequestKeyFrame,
                              NetAdaptivePressureLevel::kWatch,
                              "media_source",
                              std::string(),
                              StreamId::kMain,
                              "media_reader_slow");
        }
    }

    NetAdaptivePressureLevel ConnectionPressureLevel(
        const NetConnectionDiagnostics &connection) const {
        if (connection.pending_bytes >= options_.pending_bytes_constrained) {
            return NetAdaptivePressureLevel::kConstrained;
        }
        if (connection.pending_bytes >= options_.pending_bytes_watch ||
            connection.send_queue_length > 0) {
            return NetAdaptivePressureLevel::kWatch;
        }
        return NetAdaptivePressureLevel::kNormal;
    }

    void AddRecommendation(
        std::vector<NetAdaptiveRecommendation> *recommendations,
        NetAdaptiveRecommendationType type,
        NetAdaptivePressureLevel level,
        const std::string &protocol,
        const std::string &target,
        StreamId stream_id,
        const std::string &reason) const {
        if (recommendations == nullptr ||
            recommendations->size() >= kMaxRecommendations) {
            return;
        }
        NetAdaptiveRecommendation recommendation;
        recommendation.type = type;
        recommendation.level = level;
        recommendation.protocol = protocol;
        recommendation.target = target;
        recommendation.stream_id = stream_id;
        recommendation.reason = reason;
        recommendations->push_back(recommendation);
    }

    NetAdaptiveOptions options_;
    NetEngine *net_engine_ = nullptr;
    IRtsp *rtsp_ = nullptr;
    IWebrtc *webrtc_ = nullptr;
    IMediaSource *media_source_ = nullptr;
    bool started_ = false;
    bool stopping_ = false;
    uint64_t last_webrtc_dropped_frames_ = 0;
    std::thread sample_thread_;
    std::condition_variable condition_;
    mutable std::mutex mutex_;
    NetAdaptiveStats stats_;
    std::vector<NetAdaptiveRecommendation> recommendations_;
};

std::unique_ptr<INetAdaptive> CreateNetAdaptive(
    const NetAdaptiveOptions &options,
    const NetAdaptiveDependencies &dependencies) {
    return std::unique_ptr<INetAdaptive>(
        new NetAdaptiveImpl(options, dependencies));
}

const char *NetAdaptive::Name() { return kServiceName; }

}  // namespace live_stream
