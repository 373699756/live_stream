#include "device_impl.h"

#include "config.h"
#include "config_scopes.h"
#include "device_features.h"
#include "device_phase.h"
#include "image_tuner.h"
#include "infra/log.h"
#include "media_channels.h"
#include "media_config_codec.h"
#include "media_pipeline.h"
#include "pipeline_change.h"
#include "sdk_defaults.h"

#include <mutex>
#include <vector>

namespace live_stream {
namespace device_internal {
namespace {

using media_internal::ParseVideoConfig;
using media_internal::VerifyImageConfig;

ConfigCode MakeVerifyError(const std::string &field,
                           const std::string &msg,
                           ConfigError *error) {
    if (error != nullptr) {
        error->field = field;
        error->message = msg;
    }
    return ConfigCode::kVerify;
}

ConfigCode MakeApplyError(const std::string &field,
                          const std::string &msg,
                          ConfigError *error) {
    if (error != nullptr) {
        error->field = field;
        error->message = msg;
    }
    return ConfigCode::kApply;
}

class DeviceImpl : public DeviceMedia {
public:
    explicit DeviceImpl(const DeviceMediaOptions &device_options)
        : options_(FillDeviceDefaults(device_options)),
          pipeline_(device_options.default_config,
                    options_.sdk) {
        active_config_ = pipeline_.config();
        active_channels_ = BuildChannelsForConfig(active_config_);
        capabilities_ = pipeline_.GetCapabilities();
        pipeline_.SetFrameCallback(&DeviceImpl::OnPipelineFrame, this);
        features_.reset(new DeviceFeatures(options_, active_channels_));
        pipeline_change_.reset(new PipelineChange(pipeline_, *features_));
        image_tuner_.reset(new ImageTuner(
            [this]() { return pipeline_.QueryExposureInfo(); },
            [this](const Json &image_config) {
                std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
                return pipeline_.ApplyImageConfig(image_config);
            }));
    }

    ~DeviceImpl() override { Release(); }

    bool Start() override;
    void Stop() override;
    bool IsStarted() const override;
    bool IsRestarting() const override;
    bool IsStreamStarted(StreamId stream_id) const override;
    Codec GetStreamCodec(StreamId stream_id) const override;
    bool SetFrameSink(FrameSink *sink) override;
    bool RequestKeyframe(StreamId stream_id, KeyframeRequestSource source) override;
    MediaCapabilities GetCapabilities() const override;
    MediaChannels GetChannels() const override;
    ImageInfo GetImageInfo() const override;
    SnapshotFrame CaptureSnapshot(const SnapshotRequest &request) override;
    SnapshotInfo GetSnapshotInfo() const override;
    OverlayInfo GetOverlayInfo() const override;

private:
    bool Prepare();
    bool AttachConfigScopes(AttachedConfigs &attached_configs);
    void Release();
    static void OnPipelineFrame(const MediaFrame &frame, void *user);
    void PushFrameToSink(const MediaFrame &frame);
    ConfigCode VerifyVideoConfig(const Json &now,
                                 ConfigError *error) const;
    ConfigCode ApplyVideoConfig(const Json &prev,
                                const Json &now,
                                ConfigError *error);
    ConfigCode VerifyImageConfigScope(const Json &now,
                                      ConfigError *error) const;
    ConfigCode ApplyImageConfig(const Json &prev,
                                const Json &now,
                                ConfigError *error);
    ConfigCode CheckImageForPipelineLocked(
        const MediaPipelineConfig &pipeline_config,
        ConfigError *error) const;
    bool ApplyPipelineConfig(const MediaPipelineConfig &config);

    DeviceMediaOptions options_;
    MediaPipeline pipeline_;
    MediaPipelineConfig active_config_;
    MediaChannels active_channels_;
    MediaCapabilities capabilities_;
    DevicePhase phase_ = DevicePhase::kCreated;
    FrameSink *frame_sink_ = nullptr;
    std::unique_ptr<DeviceFeatures> features_;
    std::unique_ptr<PipelineChange> pipeline_change_;
    std::unique_ptr<ImageTuner> image_tuner_;
    ConfigScopes config_scopes_;
    mutable std::mutex mutex_;
    std::mutex pipeline_op_mutex_;
    bool system_initialized_ = false;
};

bool DeviceImpl::Prepare() {
    Json video_config;
    Json next_image_config;
    if (options_.config != nullptr) {
        video_config = options_.config->Get("video");
        next_image_config = options_.config->Get("image");
    }

    MediaPipelineConfig start_config;
    MediaCapabilities capabilities;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (IsPrepared(phase_)) {
            return true;
        }
        if (!CanPrepare(phase_)) {
            return false;
        }
        start_config = active_config_;
        capabilities = capabilities_;
    }

    if (video_config.is_object()) {
        const ConfigCode result = ParseVideoConfig(
            video_config, start_config, capabilities,
            &start_config, nullptr);
        if (result != ConfigCode::kOk) {
            return false;
        }
    }

    const bool has_image_config = next_image_config.is_object();
    if (has_image_config) {
        const ConfigCode result = VerifyImageConfig(
            next_image_config, capabilities.image, start_config,
            nullptr);
        if (result != ConfigCode::kOk) {
            return false;
        }
    }

    std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (IsPrepared(phase_)) {
            return true;
        }
        if (!CanPrepare(phase_)) {
            return false;
        }
        phase_ = DevicePhase::kStopping;
    }

    pipeline_.SetConfig(start_config);
    if (!pipeline_.InitSystem()) {
        const bool deinit_ok = pipeline_.DeinitSystem();
        std::lock_guard<std::mutex> lock(mutex_);
        if (deinit_ok) {
            phase_ = DevicePhase::kDeinitialized;
            system_initialized_ = false;
        } else {
            phase_ = DevicePhase::kFailed;
            system_initialized_ = true;
        }
        return false;
    }

    const MediaChannels start_channels = BuildChannelsForConfig(start_config);
    if (!features_->Bind(start_channels)) {
        const bool deinit_ok = pipeline_.DeinitSystem();
        std::lock_guard<std::mutex> lock(mutex_);
        if (deinit_ok) {
            phase_ = DevicePhase::kDeinitialized;
            system_initialized_ = false;
        } else {
            phase_ = DevicePhase::kFailed;
            system_initialized_ = true;
        }
        return false;
    }

    AttachedConfigs attached_configs;
    if (!AttachConfigScopes(attached_configs)) {
        const bool deinit_ok = pipeline_.DeinitSystem();
        std::lock_guard<std::mutex> lock(mutex_);
        if (deinit_ok) {
            phase_ = DevicePhase::kDeinitialized;
            system_initialized_ = false;
        } else {
            phase_ = DevicePhase::kFailed;
            system_initialized_ = true;
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_config_ = start_config;
        active_channels_ = start_channels;
        system_initialized_ = true;
        if (has_image_config) {
            image_tuner_->SetConfig(next_image_config);
        }
        phase_ = DevicePhase::kInitialized;
    }
    return true;
}

bool DeviceImpl::AttachConfigScopes(AttachedConfigs &attached_configs) {
    ConfigScope video_scope;
    video_scope.verify = [this](const Json &now, ConfigError *error) {
        std::lock_guard<std::mutex> guard(mutex_);
        return VerifyVideoConfig(now, error);
    };
    video_scope.apply = [this](const Json &prev, const Json &now,
                               ConfigError *error) {
        return ApplyVideoConfig(prev, now, error);
    };

    ConfigScope image_scope;
    image_scope.verify = [this](const Json &now, ConfigError *error) {
        std::lock_guard<std::mutex> guard(mutex_);
        return VerifyImageConfigScope(now, error);
    };
    image_scope.apply = [this](const Json &prev, const Json &now,
                               ConfigError *error) {
        return ApplyImageConfig(prev, now, error);
    };

    return config_scopes_.Attach(options_.config, video_scope, image_scope,
                                 attached_configs);
}

void DeviceImpl::Release() {
    image_tuner_->Stop();
    features_->Stop();
    features_->Release();
    bool stop_pipeline = false;
    bool deinit_pipeline = false;
    {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (phase_ == DevicePhase::kStarted) {
                phase_ = DevicePhase::kStopping;
                stop_pipeline = true;
                phase_ = DevicePhase::kStopped;
            }
            if (phase_ != DevicePhase::kDeinitialized &&
                phase_ != DevicePhase::kCreated) {
                deinit_pipeline = true;
                phase_ = DevicePhase::kStopping;
            }
        }
        if (stop_pipeline) {
            pipeline_.Stop();
        }
        if (deinit_pipeline) {
            const bool deinit_ok = pipeline_.DeinitSystem();
            std::lock_guard<std::mutex> lock(mutex_);
            if (deinit_ok) {
                phase_ = DevicePhase::kDeinitialized;
                system_initialized_ = false;
            } else {
                phase_ = DevicePhase::kFailed;
                system_initialized_ = true;
            }
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            if (phase_ != DevicePhase::kFailed) {
                system_initialized_ = false;
            }
        }
    }

    config_scopes_.Detach(options_.config);
}

void DeviceImpl::OnPipelineFrame(const MediaFrame &frame, void *user) {
    if (user != nullptr) {
        static_cast<DeviceImpl *>(user)->PushFrameToSink(frame);
    }
}

void DeviceImpl::PushFrameToSink(const MediaFrame &frame) {
    FrameSink *frame_sink = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (phase_ != DevicePhase::kStarted) {
            return;
        }
        frame_sink = frame_sink_;
    }
    if (frame_sink != nullptr) {
        (void)frame_sink->PushFrame(frame);
    }
}

ConfigCode DeviceImpl::VerifyVideoConfig(
    const Json &now,
    ConfigError *error) const {
    MediaPipelineConfig parsed;
    ConfigCode result =
        ParseVideoConfig(now, active_config_, capabilities_, &parsed, error);
    if (result != ConfigCode::kOk) {
        return result;
    }
    if (!IsValidSnapshotVencChannel(parsed)) {
        return MakeVerifyError("streams",
                               "snapshot VENC channel conflicts", error);
    }
    return CheckImageForPipelineLocked(parsed, error);
}

ConfigCode DeviceImpl::ApplyVideoConfig(const Json &prev,
                                        const Json &now,
                                        ConfigError *error) {
    (void)prev;
    MediaPipelineConfig next_config;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (phase_ == DevicePhase::kStopping) {
            return MakeApplyError("", "device media busy", error);
        }
        const ConfigCode result = ParseVideoConfig(
            now, active_config_, capabilities_, &next_config, error);
        if (result != ConfigCode::kOk) {
            return result;
        }
        if (!IsValidSnapshotVencChannel(next_config)) {
            return MakeVerifyError(
                "streams", "snapshot VENC channel conflicts", error);
        }
        const ConfigCode image_result =
            CheckImageForPipelineLocked(next_config, error);
        if (image_result != ConfigCode::kOk) {
            return image_result;
        }
    }
    if (!ApplyPipelineConfig(next_config)) {
        return MakeApplyError("streams.main", "apply failed", error);
    }
    return ConfigCode::kOk;
}

ConfigCode DeviceImpl::VerifyImageConfigScope(
    const Json &now,
    ConfigError *error) const {
    return VerifyImageConfig(now, capabilities_.image, active_config_, error);
}

ConfigCode DeviceImpl::ApplyImageConfig(const Json &prev,
                                        const Json &now,
                                        ConfigError *error) {
    (void)prev;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (phase_ != DevicePhase::kStarted) {
            image_tuner_->SetConfig(now);
            return ConfigCode::kOk;
        }
    }

    std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (phase_ != DevicePhase::kStarted) {
            image_tuner_->SetConfig(now);
            return ConfigCode::kOk;
        }
    }
    if (!pipeline_.ApplyImageConfig(now)) {
        return MakeApplyError("image", "apply failed", error);
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        image_tuner_->SetConfig(now);
    }
    return ConfigCode::kOk;
}

ConfigCode DeviceImpl::CheckImageForPipelineLocked(
    const MediaPipelineConfig &pipeline_config,
    ConfigError *error) const {
    const Json image_config = image_tuner_->GetConfig();
    if (!image_config.is_object() || image_config.empty()) {
        return ConfigCode::kOk;
    }
    // Image features such as VPSS LDC depend on stream dimensions, so video
    // config changes must be checked against the current image config.
    return VerifyImageConfig(image_config, capabilities_.image,
                             pipeline_config, error);
}

bool DeviceImpl::ApplyPipelineConfig(
    const MediaPipelineConfig &config) {
    bool is_started = false;
    bool system_initialized = false;
    DevicePhase prev_phase = DevicePhase::kCreated;
    MediaPipelineConfig prev_config;
    Json prev_image_config;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (phase_ == DevicePhase::kStopping ||
            phase_ == DevicePhase::kFailed) {
            return false;
        }
        prev_phase = phase_;
        is_started = phase_ == DevicePhase::kStarted;
        system_initialized = system_initialized_;
        prev_config = active_config_;
        prev_image_config = image_tuner_->GetConfig();
        phase_ = DevicePhase::kStopping;
    }

    if (is_started) {
        image_tuner_->Stop();
    }

    PipelineChangeInfo change_info;
    {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
        if (is_started) {
            features_->Stop();
        }
        PipelineChangePlan plan;
        plan.next_config = config;
        plan.prev_config = prev_config;
        plan.prev_image_config = prev_image_config;
        plan.is_started = is_started;
        plan.system_initialized = system_initialized;
        change_info = pipeline_change_->Apply(plan);
    }

    if (change_info.applied) {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            active_config_ = config;
            active_channels_ = BuildChannelsForConfig(active_config_);
            system_initialized_ = system_initialized;
            phase_ = is_started ? DevicePhase::kStarted
                                : prev_phase;
            if (is_started) {
                image_tuner_->Start();
            }
        }
        return true;
    }

    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (change_info.restored) {
            active_config_ = prev_config;
            active_channels_ = BuildChannelsForConfig(active_config_);
            system_initialized_ = system_initialized;
            phase_ = is_started ? DevicePhase::kStarted
                                : prev_phase;
            if (is_started) {
                image_tuner_->Start();
            }
        } else {
            if (system_initialized) {
                system_initialized_ = true;
                phase_ = DevicePhase::kFailed;
            } else if (is_started) {
                system_initialized_ = false;
                phase_ = DevicePhase::kStopped;
            } else {
                system_initialized_ = false;
                phase_ = prev_phase;
            }
        }
    }
    return false;
}

bool DeviceImpl::Start() {
    bool need_prepare = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        need_prepare = CanPrepare(phase_);
    }
    if (need_prepare && !Prepare()) {
        return false;
    }

    Json prev_image_config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (phase_ == DevicePhase::kStarted) {
            return true;
        }
        if (phase_ == DevicePhase::kStopped) {
            phase_ = DevicePhase::kInitialized;
        }
        if (phase_ != DevicePhase::kInitialized) {
            return false;
        }
        phase_ = DevicePhase::kStopping;
        prev_image_config = image_tuner_->GetConfig();
    }

    auto mark_start_failed = [this]() {
        std::lock_guard<std::mutex> lock(mutex_);
        phase_ = DevicePhase::kInitialized;
        return false;
    };

    {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);

        auto rollback_started_pipeline = [this]() {
            features_->Stop();
            pipeline_.Stop();
        };

        if (!pipeline_.Start()) {
            rollback_started_pipeline();
            return mark_start_failed();
        }
        if (prev_image_config.is_object() && !prev_image_config.empty() &&
            !pipeline_.ApplyImageConfig(prev_image_config)) {
            rollback_started_pipeline();
            return mark_start_failed();
        }
        if (!features_->Start()) {
            rollback_started_pipeline();
            return mark_start_failed();
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        phase_ = DevicePhase::kStarted;
        image_tuner_->Start();
    }
    return true;
}

void DeviceImpl::Stop() {
    image_tuner_->Stop();
    features_->Stop();
    bool should_stop = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (phase_ != DevicePhase::kStarted) {
            if (phase_ == DevicePhase::kStopping) {
                phase_ = DevicePhase::kStopped;
            }
            return;
        }

        phase_ = DevicePhase::kStopping;
        should_stop = true;
    }

    if (should_stop) {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
        pipeline_.Stop();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    phase_ = DevicePhase::kStopped;
}

bool DeviceImpl::IsStarted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return phase_ == DevicePhase::kStarted;
}

bool DeviceImpl::IsRestarting() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return phase_ == DevicePhase::kStopping;
}

bool DeviceImpl::IsStreamStarted(StreamId stream_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const VideoStreamConfig *stream =
        FindConfiguredStream(active_config_, stream_id);
    return phase_ == DevicePhase::kStarted && stream != nullptr &&
           stream->enabled;
}

Codec DeviceImpl::GetStreamCodec(StreamId stream_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const VideoStreamConfig *stream =
        FindConfiguredStream(active_config_, stream_id);
    if (stream != nullptr) {
        return stream->codec;
    }
    return Codec::kH264;
}

bool DeviceImpl::SetFrameSink(FrameSink *sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_sink_ = sink;
    return true;
}

bool DeviceImpl::RequestKeyframe(StreamId stream_id,
                                 KeyframeRequestSource source) {
    (void)source;
    int32_t venc_channel = -1;
    hisisdk::IHisiVencStream *venc_stream = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const MediaPipelineConfig config = active_config_;
        const VideoStreamConfig *stream =
            FindConfiguredStream(config, stream_id);
        if (phase_ != DevicePhase::kStarted || stream == nullptr ||
            !stream->enabled) {
            return false;
        }
        venc_channel = VencChannelForStream(config, stream_id);
        venc_stream = options_.sdk.venc_stream;
    }
    return venc_stream != nullptr && venc_stream->RequestIdr(venc_channel);
}

MediaCapabilities DeviceImpl::GetCapabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capabilities_;
}

MediaChannels DeviceImpl::GetChannels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!system_initialized_ || phase_ != DevicePhase::kStarted) {
        return MediaChannels{};
    }
    return active_channels_;
}

ImageInfo DeviceImpl::GetImageInfo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return image_tuner_->GetInfo();
}

SnapshotFrame DeviceImpl::CaptureSnapshot(
    const SnapshotRequest &request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (phase_ != DevicePhase::kStarted) {
            return SnapshotFrame{};
        }
    }
    std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (phase_ != DevicePhase::kStarted) {
            return SnapshotFrame{};
        }
    }
    return features_->CaptureSnapshot(request);
}

SnapshotInfo DeviceImpl::GetSnapshotInfo() const {
    return features_->GetSnapshotInfo();
}

OverlayInfo DeviceImpl::GetOverlayInfo() const {
    return features_->GetOverlayInfo();
}

}  // namespace

std::unique_ptr<DeviceMedia> CreateDeviceMediaImpl(
    const DeviceMediaOptions &options) {
    return std::unique_ptr<DeviceMedia>(new DeviceImpl(options));
}

}  // namespace device_internal
}  // namespace live_stream
