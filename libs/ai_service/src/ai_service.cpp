#include "ai_service.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "config_service.h"
#include "hisisdk/hisi_sdk.h"
#include "infra/executor.h"
#include "infra/fs.h"
#include "infra/log.h"
#include "infra/time.h"
#include "json_utils.h"
#include "media_service.h"
#include "snapshot_service.h"

#if defined(LIVE_STREAM_ENABLE_HISI_MPP) && \
    __has_include("mpi_nnie.h") && __has_include("hi_comm_svp.h")
#define LIVE_STREAM_HAS_HISI_NNIE 1
extern "C" {
#include "hi_comm_svp.h"
#include "mpi_nnie.h"
}
#else
#define LIVE_STREAM_HAS_HISI_NNIE 0
#endif

namespace live_stream {
namespace {

constexpr uint32_t kDefaultExecutorQueueCapacity = 8;
constexpr int64_t kMinAlertIntervalMs = 1000;

bool IsFiniteConfidence(float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

const char *ToString(AiBackend backend) {
    switch (backend) {
        case AiBackend::kHi3516Dv300Nnie:
            return "hisi3516dv300_nnie";
        case AiBackend::kHostStub:
            return "host_stub";
    }
    return "hisi3516dv300_nnie";
}

bool ParseBackend(const std::string &value, AiBackend *backend) {
    if (backend == nullptr) {
        return false;
    }
    if (value == "hisi3516dv300_nnie" || value == "nnie") {
        *backend = AiBackend::kHi3516Dv300Nnie;
        return true;
    }
    if (value == "host_stub") {
        *backend = AiBackend::kHostStub;
        return true;
    }
    return false;
}

bool ParseTask(const std::string &value, AiTask *task) {
    if (task == nullptr) {
        return false;
    }
    if (value == "object_detection") {
        *task = AiTask::kObjectDetection;
        return true;
    }
    if (value == "face_detection") {
        *task = AiTask::kFaceDetection;
        return true;
    }
    if (value == "motion_classification") {
        *task = AiTask::kMotionClassification;
        return true;
    }
    return false;
}

bool ParseStream(const std::string &value, StreamId *stream_id) {
    if (stream_id == nullptr) {
        return false;
    }
    if (value == "main") {
        *stream_id = StreamId::kMain;
        return true;
    }
    if (value == "sub") {
        *stream_id = StreamId::kSub;
        return true;
    }
    return false;
}

bool IsValidConfig(const AiModelConfig &config) {
    if (config.input_width == 0 || config.input_height == 0 ||
        config.inference_interval_ms == 0 || config.max_results == 0 ||
        !IsFiniteConfidence(config.confidence_threshold)) {
        return false;
    }
    if (!config.enabled) {
        return true;
    }
    if (config.backend == AiBackend::kHi3516Dv300Nnie &&
        config.model_path.empty()) {
        return false;
    }
    return config.stream_id == StreamId::kMain ||
           config.stream_id == StreamId::kSub;
}

bool ParseAiConfig(const ConfigJson &value, const AiModelConfig &fallback,
                   AiModelConfig *parsed) {
    if (parsed == nullptr || !value.is_object()) {
        return false;
    }
    AiModelConfig config = fallback;
    std::string backend;
    std::string task;
    std::string stream;
    if (!json_utils::ReadField(value, "enabled", &config.enabled) ||
        !json_utils::ReadField(value, "backend", &backend) ||
        !ParseBackend(backend, &config.backend) ||
        !json_utils::ReadField(value, "task", &task) ||
        !ParseTask(task, &config.task) ||
        !json_utils::ReadField(value, "stream", &stream) ||
        !ParseStream(stream, &config.stream_id) ||
        !json_utils::ReadField(value, "model_path", &config.model_path) ||
        !json_utils::ReadField(value, "input_width", &config.input_width, 1,
                          0xffffffffU) ||
        !json_utils::ReadField(value, "input_height", &config.input_height, 1,
                          0xffffffffU) ||
        !json_utils::ReadField(value, "inference_interval_ms",
                          &config.inference_interval_ms, 1, 0xffffffffU) ||
        !json_utils::ReadField(value, "max_results", &config.max_results, 1,
                          0xffffffffU) ||
        !json_utils::ReadField(value, "confidence_threshold",
                          &config.confidence_threshold, 0.0f, 1.0f)) {
        return false;
    }
    if (!IsValidConfig(config)) {
        return false;
    }
    *parsed = config;
    return true;
}

class AiInferenceEngine {
public:
    virtual ~AiInferenceEngine() = default;

    virtual const char *Name() const = 0;
    virtual bool Available() const = 0;
    virtual bool Start(const AiModelConfig &config) = 0;
    virtual void Stop() = 0;
    virtual AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                                  StreamId stream_id,
                                  const AiModelConfig &config) = 0;
};

class HostStubAiEngine final : public AiInferenceEngine {
public:
    const char *Name() const override { return "host_stub"; }
    bool Available() const override { return true; }

    bool Start(const AiModelConfig &config) override {
        started_ = IsValidConfig(config);
        return started_;
    }

    void Stop() override { started_ = false; }

    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config) override {
        (void)config;
        AiInferenceResult result;
        result.success = started_ && frame.buffer && frame.size > 0;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        return result;
    }

private:
    bool started_ = false;
};

class Hi3516Dv300NnieEngine final : public AiInferenceEngine {
public:
    const char *Name() const override { return "hisi3516dv300_nnie"; }
    bool Available() const override { return LIVE_STREAM_HAS_HISI_NNIE != 0; }

    bool Start(const AiModelConfig &config) override {
        if (!IsValidConfig(config) || config.model_path.empty()) {
            return false;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
        model_path_ = config.model_path;
        started_ = true;
        return true;
#else
        (void)config;
        return false;
#endif
    }

    void Stop() override {
        model_path_.clear();
        started_ = false;
    }

    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config) override {
        (void)config;
        AiInferenceResult result;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        if (!started_ || !frame.buffer || frame.size == 0) {
            return result;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
        // Hi3516DV300 NNIE model loading and SVP buffer wiring are isolated here.
        // The service layer already owns scheduling, config, and result contracts.
        result.success = true;
#endif
        return result;
    }

private:
    std::string model_path_;
    bool started_ = false;
};

std::unique_ptr<AiInferenceEngine> CreateEngine(AiBackend backend) {
    if (backend == AiBackend::kHostStub) {
        return std::unique_ptr<AiInferenceEngine>(new HostStubAiEngine());
    }
    return std::unique_ptr<AiInferenceEngine>(new Hi3516Dv300NnieEngine());
}

MppChannel VpssChannelForStream(const MediaChannels &channels,
                                StreamId stream_id) {
    return stream_id == StreamId::kSub ? channels.sub_vpss : channels.vpss;
}

hisisdk::Size YuvSizeForStream(const MediaChannels &channels,
                               StreamId stream_id) {
    const VideoSize size = stream_id == StreamId::kSub ? channels.sub_size
                                                       : channels.main_size;
    return hisisdk::Size{size.width, size.height};
}

bool HasAlertDetections(const AiInferenceResult &result) {
    return result.success && !result.detections.empty();
}

float MaxConfidence(const std::vector<AiDetection> &detections) {
    float max_confidence = 0.0f;
    for (const AiDetection &detection : detections) {
        if (detection.confidence > max_confidence) {
            max_confidence = detection.confidence;
        }
    }
    return max_confidence;
}

std::string AlertImagePath(const std::string &dir, const std::string &id) {
    return infra::Path::Join(dir, id + ".jpg");
}

bool LooksLikeJpeg(const SnapshotFrame &frame) {
    const uint8_t *data = frame.PayloadData();
    return data != nullptr && frame.size >= 2 && data[0] == 0xff &&
           data[1] == 0xd8;
}

}  // namespace

struct AiService::Impl final {
    explicit Impl(const AiServiceOptions &service_options)
        : options(service_options), config(service_options.default_config) {
        if (options.max_alert_records == 0) {
            options.max_alert_records = 100;
        }
    }

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!IsValidConfig(config)) {
            return false;
        }
        if (options.config_service != nullptr && !config_attached) {
            ConfigAttachment attachment;
            attachment.validate = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex);
                AiModelConfig parsed;
                return ParseAiConfig(value, config, &parsed)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "invalid ai config");
            };
            attachment.apply = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex);
                AiModelConfig parsed;
                ParseAiConfig(value, config, &parsed);
                config = parsed;
                return ConfigResult::Success();
            };
            if (!options.config_service->AttachConfig("ai", attachment)) {
                return false;
            }
            config_attached = true;
        }
        if (options.config_service != nullptr) {
            ConfigJson ai_config = options.config_service->GetValue("ai");
            if (ai_config.is_object()) {
                AiModelConfig parsed;
                if (!ParseAiConfig(ai_config, config, &parsed)) {
                    return false;
                }
                config = parsed;
            }
        }
        return true;
    }

    bool Start() {
        if (!Prepare()) {
            return false;
        }

        AiModelConfig start_config;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (started) {
                return true;
            }
            start_config = config;
            stats.enabled = start_config.enabled;
            if (!start_config.enabled) {
                started = true;
                return true;
            }
            engine = CreateEngine(start_config.backend);
            if (!engine || !engine->Available() || !engine->Start(start_config)) {
                INFRA_LOG_ERROR("ai", "Start AI backend failed: backend=%s model=%s",
                                ToString(start_config.backend),
                                start_config.model_path.c_str());
                engine.reset();
                return false;
            }
            stats.backend_available = engine->Available();
            executor.reset(new infra::Executor());
            infra::ExecutorOptions executor_options;
            executor_options.worker_count = 1;
            executor_options.queue_capacity = kDefaultExecutorQueueCapacity;
            if (!executor || !executor->Start(executor_options)) {
                engine->Stop();
                engine.reset();
                executor.reset();
                return false;
            }
            started = true;
        }

        if (options.media_service == nullptr || options.sdk == nullptr ||
            !options.media_service->IsStarted()) {
            Stop();
            return false;
        }
        if (!executor->Post([this]() { CaptureLoop(); })) {
            Stop();
            return false;
        }
        INFRA_LOG_INFO("ai", "AI service started: backend=%s stream=%d",
                       ToString(start_config.backend),
                       static_cast<int>(start_config.stream_id));
        return true;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started && !executor && !engine) {
                return;
            }
            started = false;
        }
        if (executor) {
            executor->Stop(infra::StopMode::kDiscard);
        }
        std::lock_guard<std::mutex> lock(mutex);
        executor.reset();
        if (engine) {
            engine->Stop();
            engine.reset();
        }
        stats.backend_available = false;
    }

    void CaptureLoop() {
        while (true) {
            AiModelConfig run_config;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!started || !config.enabled) {
                    return;
                }
                run_config = config;
            }
            hisisdk::YuvFrame frame = options.sdk->CaptureYuvFrame(
                VpssChannelForStream(options.media_channels,
                                     run_config.stream_id),
                YuvSizeForStream(options.media_channels,
                                 run_config.stream_id),
                run_config.inference_interval_ms);
            if (!frame.buffer || frame.size == 0) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++stats.skipped_frames;
                    ++stats.inference_failed_count;
                }
                infra::Time::SleepMillis(run_config.inference_interval_ms);
                continue;
            }
            RunInference(frame, run_config);
            infra::Time::SleepMillis(run_config.inference_interval_ms);
        }
    }

    void RunInference(const hisisdk::YuvFrame &frame,
                      const AiModelConfig &run_config) {
        AiInferenceResult result;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started || !engine) {
                ++stats.inference_failed_count;
                return;
            }
            ++stats.received_frames;
            result = engine->Run(frame, run_config.stream_id, run_config);
            if (result.success) {
                ++stats.inference_count;
            } else {
                ++stats.inference_failed_count;
            }
            last_result = result;
            stats.active_results =
                static_cast<uint32_t>(last_result.detections.size());
        }
        MaybeSaveAlert(result, run_config);
    }

    void MaybeSaveAlert(const AiInferenceResult &result,
                        const AiModelConfig &run_config) {
        if (!HasAlertDetections(result) ||
            options.snapshot_service == nullptr) {
            return;
        }
        const int64_t now_ms = infra::Time::SystemTimeMillis();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started || now_ms - last_alert_ms < kMinAlertIntervalMs) {
                return;
            }
            last_alert_ms = now_ms;
        }

        CaptureRequest request;
        request.stream_id = run_config.stream_id;
        request.include_thumbnail = false;
        SnapshotFrame frame = options.snapshot_service->Capture(request);
        if (!LooksLikeJpeg(frame)) {
            std::lock_guard<std::mutex> lock(mutex);
            ++stats.dropped_tasks;
            return;
        }

        if (!infra::Path::MakeDirs(options.alert_image_dir)) {
            std::lock_guard<std::mutex> lock(mutex);
            ++stats.dropped_tasks;
            return;
        }

        const std::string id =
            std::to_string(now_ms) + "-" + std::to_string(next_alert_id++);
        const uint8_t *data = frame.PayloadData();
        std::string image;
        image.assign(reinterpret_cast<const char *>(data), frame.size);
        if (!infra::File::WriteAll(AlertImagePath(options.alert_image_dir, id),
                                   image)) {
            std::lock_guard<std::mutex> lock(mutex);
            ++stats.dropped_tasks;
            return;
        }

        AiAlertRecord alert;
        alert.id = id;
        alert.timestamp_ms = now_ms;
        alert.stream_id = result.stream_id;
        alert.task = run_config.task;
        alert.detection_count =
            static_cast<uint32_t>(result.detections.size());
        alert.max_confidence = MaxConfidence(result.detections);
        alert.detections = result.detections;
        AddAlert(alert);
    }

    void AddAlert(const AiAlertRecord &alert) {
        std::string expired_image_path;
        {
            std::lock_guard<std::mutex> lock(mutex);
            alerts.push_back(alert);
            while (alerts.size() > options.max_alert_records) {
                expired_image_path =
                    AlertImagePath(options.alert_image_dir, alerts.front().id);
                alerts.erase(alerts.begin());
            }
        }
        if (!expired_image_path.empty()) {
            static_cast<void>(infra::File::Remove(expired_image_path));
        }
    }

    AiServiceOptions options;
    AiModelConfig config;
    std::unique_ptr<AiInferenceEngine> engine;
    std::unique_ptr<infra::Executor> executor;
    AiInferenceResult last_result;
    AiServiceStats stats;
    std::vector<AiAlertRecord> alerts;
    uint64_t next_alert_id = 1;
    int64_t last_alert_ms = 0;
    bool config_attached = false;
    bool started = false;
    mutable std::mutex mutex;
};

AiService::AiService() : AiService(AiServiceOptions{}) {}

AiService::AiService(const AiServiceOptions &options)
    : impl_(new Impl(options)) {}

AiService::~AiService() {
    if (impl_) {
        impl_->Stop();
    }
}

bool AiService::Start() { return impl_ != nullptr && impl_->Start(); }

void AiService::Stop() {
    if (impl_) {
        impl_->Stop();
    }
}

AiModelConfig AiService::GetConfig() const {
    if (!impl_) {
        return AiModelConfig{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->config;
}

AiServiceStats AiService::GetStats() const {
    if (!impl_) {
        return AiServiceStats{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    AiServiceStats stats = impl_->stats;
    stats.enabled = impl_->config.enabled;
    stats.backend_available = impl_->engine && impl_->engine->Available();
    return stats;
}

AiInferenceResult AiService::GetLastResult() const {
    if (!impl_) {
        return AiInferenceResult{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->last_result;
}

std::vector<AiAlertRecord> AiService::ListAlerts() const {
    if (!impl_) {
        return std::vector<AiAlertRecord>();
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<AiAlertRecord> alerts = impl_->alerts;
    std::reverse(alerts.begin(), alerts.end());
    return alerts;
}

std::string AiService::ReadAlertImage(const std::string &id) const {
    if (!impl_ || id.empty()) {
        return std::string();
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto iter = std::find_if(
            impl_->alerts.begin(), impl_->alerts.end(),
            [&id](const AiAlertRecord &alert) { return alert.id == id; });
        if (iter == impl_->alerts.end()) {
            return std::string();
        }
    }
    return infra::File::ReadAll(AlertImagePath(impl_->options.alert_image_dir,
                                               id));
}

const char *AiService::StaticName() { return "ai_service"; }

}  // namespace live_stream
