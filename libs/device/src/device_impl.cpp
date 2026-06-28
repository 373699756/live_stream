#include "device_impl.h"

#include "config.h"
#include "device_phase.h"
#include "image_strategy.h"
#include "infra/log.h"
#include "media_channels.h"
#include "media_config_codec.h"
#include "media_pipeline.h"
#include "region_overlay.h"
#include "sdk_defaults.h"
#include "snapshot.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace live_stream {
namespace device_internal {
namespace {

using media_internal::ParseVideoConfig;
using media_internal::VerifyImageConfig;

constexpr int kImageStrategyIntervalMs = 1000;

ConfigCode RejectConfigVerify(const std::string &field,
                                const std::string &reason,
                                ConfigError *error) {
    if (error != nullptr) {
        error->field = field;
        error->message = reason;
    }
    return ConfigCode::kVerify;
}

ConfigCode RejectConfigApply(const std::string &field,
                               const std::string &reason,
                               ConfigError *error) {
    if (error != nullptr) {
        error->field = field;
        error->message = reason;
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

        SnapshotConfig snapshot_config;
        snapshot_config.jpeg_venc_channel = device_options.snapshot_venc_channel;
        SnapshotOptions snapshot_options;
        snapshot_options.default_config = snapshot_config;
        snapshot_options.config = device_options.config;
        snapshot_options.media_channels = active_channels_;
        snapshot_options.snapshot = options_.sdk.snapshot;
        snapshot_.reset(new Snapshot(snapshot_options));

        RegionOverlayOptions overlay_options;
        overlay_options.config = device_options.config;
        overlay_options.media_channels = active_channels_;
        overlay_options.region = options_.sdk.region;
        region_overlay_.reset(new RegionOverlay(overlay_options));
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
    struct AttachedConfigs {
        bool video = false;
        bool image = false;
    };

    bool Prepare();
    bool AddConfigScopes(AttachedConfigs &attached_now);
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
    bool ApplyImageToPipeline(const Json &value);
    Json BuildAutoImageConfigLocked(
        const hisisdk::ExposureInfo &exposure,
        ImageInfo &next_info) const;
    void StartImageTunerLocked();
    void StopImageTuner();
    void RunImageTuner();
    bool ApplyPipelineConfig(const MediaPipelineConfig &config);
    bool BindDeviceFeatures(const MediaChannels &channels);
    bool StartDeviceFeatures();
    void StopDeviceFeatures();
    void ReleaseDeviceFeatures();

    DeviceMediaOptions options_;
    MediaPipeline pipeline_;
    MediaPipelineConfig active_config_;
    MediaChannels active_channels_;
    MediaCapabilities capabilities_;
    DevicePhase phase_ = DevicePhase::kCreated;
    FrameSink *frame_sink_ = nullptr;
    Json image_config_ = Json::object();
    ImageInfo image_info_;
    std::unique_ptr<Snapshot> snapshot_;
    std::unique_ptr<RegionOverlay> region_overlay_;
    mutable std::mutex mutex_;
    std::mutex pipeline_op_mutex_;
    bool video_config_attached_ = false;
    bool image_config_attached_ = false;
    bool system_initialized_ = false;
    std::thread image_tuner_thread_;
    bool image_tuner_running_ = false;
    bool stop_image_tuner_ = false;
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
    if (!BindDeviceFeatures(start_channels)) {
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

    AttachedConfigs attached_now;
    if (!AddConfigScopes(attached_now)) {
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
            image_config_ = next_image_config;
        }
        if (attached_now.video) {
            video_config_attached_ = true;
        }
        if (attached_now.image) {
            image_config_attached_ = true;
        }
        phase_ = DevicePhase::kInitialized;
    }
    return true;
}

bool DeviceImpl::AddConfigScopes(AttachedConfigs &attached_now) {
    attached_now = AttachedConfigs{};
    if (options_.config == nullptr) {
        return true;
    }

    bool need_video_scope = false;
    bool need_image_scope = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        need_video_scope = !video_config_attached_;
        need_image_scope = !image_config_attached_;
    }

    if (need_video_scope) {
        ConfigScope config_scope;
        config_scope.verify = [this](const Json &now,
                                     ConfigError *error) {
            std::lock_guard<std::mutex> guard(mutex_);
            return VerifyVideoConfig(now, error);
        };
        config_scope.apply = [this](const Json &prev,
                                    const Json &now,
                                    ConfigError *error) {
            return ApplyVideoConfig(prev, now, error);
        };
        if (!options_.config->AddScope("video", config_scope)) {
            return false;
        }
        attached_now.video = true;
    }

    if (need_image_scope) {
        ConfigScope config_scope;
        config_scope.verify = [this](const Json &now,
                                     ConfigError *error) {
            std::lock_guard<std::mutex> guard(mutex_);
            return VerifyImageConfigScope(now, error);
        };
        config_scope.apply = [this](const Json &prev,
                                    const Json &now,
                                    ConfigError *error) {
            return ApplyImageConfig(prev, now, error);
        };
        if (!options_.config->AddScope("image", config_scope)) {
            if (attached_now.video) {
                (void)options_.config->RemoveScope("video");
            }
            attached_now = AttachedConfigs{};
            return false;
        }
        attached_now.image = true;
    }
    return true;
}

bool DeviceImpl::BindDeviceFeatures(const MediaChannels &channels) {
    if (!snapshot_->BindMedia(channels)) {
        return false;
    }
    if (!region_overlay_->BindMedia(channels)) {
        return false;
    }
    return true;
}

bool DeviceImpl::StartDeviceFeatures() {
    if (!snapshot_->Start()) {
        return false;
    }
    if (!region_overlay_->Start()) {
        snapshot_->Stop();
        return false;
    }
    return true;
}

void DeviceImpl::StopDeviceFeatures() {
    region_overlay_->Stop();
    snapshot_->Stop();
}

void DeviceImpl::ReleaseDeviceFeatures() {
    region_overlay_->Release();
    snapshot_->Release();
}

void DeviceImpl::Release() {
    StopImageTuner();
    StopDeviceFeatures();
    ReleaseDeviceFeatures();
    bool detach_video = false;
    bool detach_image = false;
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
            detach_video = video_config_attached_;
            detach_image = image_config_attached_;
            video_config_attached_ = false;
            image_config_attached_ = false;
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

    if (options_.config != nullptr) {
        if (detach_video) {
            (void)options_.config->RemoveScope("video");
        }
        if (detach_image) {
            (void)options_.config->RemoveScope("image");
        }
    }
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
        return RejectConfigVerify("streams",
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
            return RejectConfigApply("", "device media busy", error);
        }
        const ConfigCode result = ParseVideoConfig(
            now, active_config_, capabilities_, &next_config, error);
        if (result != ConfigCode::kOk) {
            return result;
        }
        if (!IsValidSnapshotVencChannel(next_config)) {
            return RejectConfigVerify(
                "streams", "snapshot VENC channel conflicts", error);
        }
        const ConfigCode image_result =
            CheckImageForPipelineLocked(next_config, error);
        if (image_result != ConfigCode::kOk) {
            return image_result;
        }
    }
    if (!ApplyPipelineConfig(next_config)) {
        return RejectConfigApply("streams.main", "apply failed", error);
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
            image_config_ = now;
            return ConfigCode::kOk;
        }
    }

    std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (phase_ != DevicePhase::kStarted) {
            image_config_ = now;
            return ConfigCode::kOk;
        }
    }
    if (!pipeline_.ApplyImageConfig(now)) {
        return RejectConfigApply("image", "apply failed", error);
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        image_config_ = now;
    }
    return ConfigCode::kOk;
}

ConfigCode DeviceImpl::CheckImageForPipelineLocked(
    const MediaPipelineConfig &pipeline_config,
    ConfigError *error) const {
    if (!image_config_.is_object() || image_config_.empty()) {
        return ConfigCode::kOk;
    }
    // Image features such as VPSS LDC depend on stream dimensions, so video
    // config changes must be checked against the current image config.
    return VerifyImageConfig(image_config_, capabilities_.image,
                             pipeline_config, error);
}

bool DeviceImpl::ApplyImageToPipeline(
    const Json &value) {
    if (!value.is_object() || value.empty()) {
        return true;
    }
    return pipeline_.ApplyImageConfig(value);
}

Json DeviceImpl::BuildAutoImageConfigLocked(
    const hisisdk::ExposureInfo &exposure,
    ImageInfo &next_info) const {
    return BuildImageStrategyConfig(image_config_, image_info_,
                                    exposure, next_info);
}

void DeviceImpl::StartImageTunerLocked() {
    if (image_tuner_running_) {
        return;
    }
    stop_image_tuner_ = false;
    image_tuner_running_ = true;
    image_tuner_thread_ =
        std::thread(&DeviceImpl::RunImageTuner, this);
}

void DeviceImpl::StopImageTuner() {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        stop_image_tuner_ = true;
    }
    if (image_tuner_thread_.joinable()) {
        image_tuner_thread_.join();
    }
    std::lock_guard<std::mutex> guard(mutex_);
    image_tuner_running_ = false;
    stop_image_tuner_ = false;
    image_info_.active = false;
}

void DeviceImpl::RunImageTuner() {
    while (true) {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (stop_image_tuner_) {
                return;
            }
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kImageStrategyIntervalMs));

        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (stop_image_tuner_) {
                return;
            }
            const bool strategy_enabled =
                IsImageStrategyEnabled(image_config_);
            image_info_.enabled = strategy_enabled;
            if (phase_ != DevicePhase::kStarted || !strategy_enabled) {
                image_info_.active = false;
                continue;
            }
        }

        const hisisdk::ExposureInfo exposure = pipeline_.QueryExposureInfo();
        if (!exposure.valid) {
            std::lock_guard<std::mutex> guard(mutex_);
            if (stop_image_tuner_) {
                return;
            }
            image_info_.exposure_valid = false;
            continue;
        }

        ImageInfo next_info;
        Json adjusted;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (stop_image_tuner_ ||
                phase_ != DevicePhase::kStarted ||
                !IsImageStrategyEnabled(image_config_)) {
                continue;
            }
            adjusted = BuildAutoImageConfigLocked(exposure,
                                                      next_info);
        }

        bool applied = false;
        {
            std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
            bool can_apply = false;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                can_apply = !stop_image_tuner_ &&
                            phase_ == DevicePhase::kStarted &&
                            IsImageStrategyEnabled(image_config_);
            }
            if (can_apply) {
                applied = pipeline_.ApplyImageConfig(adjusted);
            }
        }
        if (applied) {
            std::lock_guard<std::mutex> guard(mutex_);
            image_info_ = next_info;
        }
    }
}

bool DeviceImpl::ApplyPipelineConfig(
    const MediaPipelineConfig &config) {
    bool is_started = false;
    bool has_system = false;
    DevicePhase prev_run_state = DevicePhase::kCreated;
    MediaPipelineConfig prev_config;
    Json prev_image_config;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (phase_ == DevicePhase::kStopping ||
            phase_ == DevicePhase::kFailed) {
            return false;
        }
        prev_run_state = phase_;
        is_started = phase_ == DevicePhase::kStarted;
        has_system = system_initialized_;
        prev_config = active_config_;
        prev_image_config = image_config_;
        phase_ = DevicePhase::kStopping;
    }

    if (is_started) {
        StopImageTuner();
        StopDeviceFeatures();
    }

    bool applied = false;
    bool restored = false;
    {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
        if (is_started) {
            pipeline_.Stop();
        }
        bool deinit_ok = true;
        if (has_system) {
            deinit_ok = pipeline_.DeinitSystem();
        }

        if (!deinit_ok) {
            applied = false;
        } else {
            pipeline_.SetConfig(config);
            const MediaChannels next_channels = BuildChannelsForConfig(config);

            applied = true;
            if (has_system && !pipeline_.InitSystem()) {
                applied = false;
            } else if (!BindDeviceFeatures(next_channels)) {
                applied = false;
            } else if (is_started && !pipeline_.Start()) {
                applied = false;
            } else if (is_started &&
                       !ApplyImageToPipeline(prev_image_config)) {
                applied = false;
            } else if (is_started && !StartDeviceFeatures()) {
                applied = false;
            }
        }

        if (!applied && deinit_ok &&
            (is_started || has_system)) {
            StopDeviceFeatures();
            pipeline_.Stop();
            if (has_system) {
                (void)pipeline_.DeinitSystem();
            }
            pipeline_.SetConfig(prev_config);
            const MediaChannels prev_channels =
                BuildChannelsForConfig(prev_config);
            bool restore_ok = true;
            if (has_system && !pipeline_.InitSystem()) {
                restore_ok = false;
            }
            if (restore_ok && !BindDeviceFeatures(prev_channels)) {
                restore_ok = false;
            }
            if (restore_ok && is_started && !pipeline_.Start()) {
                restore_ok = false;
            }
            if (restore_ok && is_started &&
                !ApplyImageToPipeline(prev_image_config)) {
                restore_ok = false;
            }
            if (restore_ok && is_started && !StartDeviceFeatures()) {
                restore_ok = false;
            }
            if (!restore_ok) {
                StopDeviceFeatures();
                pipeline_.Stop();
                if (has_system) {
                    (void)pipeline_.DeinitSystem();
                }
                Error("device",
                      "restore media pipeline after config failure failed");
            } else {
                Error("device",
                      "media config apply failed, restored previous pipeline");
            }
            restored = restore_ok;
        }
    }

    if (applied) {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            active_config_ = config;
            active_channels_ = BuildChannelsForConfig(active_config_);
            system_initialized_ = has_system;
            phase_ = is_started ? DevicePhase::kStarted
                                        : prev_run_state;
            if (is_started) {
                StartImageTunerLocked();
            }
        }
        return true;
    }

    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (restored) {
            active_config_ = prev_config;
            active_channels_ = BuildChannelsForConfig(active_config_);
            system_initialized_ = has_system;
            phase_ = is_started ? DevicePhase::kStarted
                                        : prev_run_state;
            if (is_started) {
                StartImageTunerLocked();
            }
        } else {
            if (has_system) {
                system_initialized_ = true;
                phase_ = DevicePhase::kFailed;
            } else if (is_started) {
                system_initialized_ = false;
                phase_ = DevicePhase::kStopped;
            } else {
                system_initialized_ = false;
                phase_ = prev_run_state;
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
        prev_image_config = image_config_;
    }

    auto mark_start_failed = [this]() {
        std::lock_guard<std::mutex> lock(mutex_);
        phase_ = DevicePhase::kInitialized;
        return false;
    };

    {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);

        auto rollback_started_pipeline = [this]() {
            StopDeviceFeatures();
            pipeline_.Stop();
        };

        if (!pipeline_.Start()) {
            rollback_started_pipeline();
            return mark_start_failed();
        }
        if (!ApplyImageToPipeline(prev_image_config)) {
            rollback_started_pipeline();
            return mark_start_failed();
        }
        if (!StartDeviceFeatures()) {
            rollback_started_pipeline();
            return mark_start_failed();
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        phase_ = DevicePhase::kStarted;
        StartImageTunerLocked();
    }
    return true;
}

void DeviceImpl::Stop() {
    StopImageTuner();
    StopDeviceFeatures();
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
    if (!system_initialized_ || phase_ == DevicePhase::kFailed) {
        return MediaChannels{};
    }
    return active_channels_;
}

ImageInfo DeviceImpl::GetImageInfo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return image_info_;
}

SnapshotFrame DeviceImpl::CaptureSnapshot(
    const SnapshotRequest &request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (phase_ != DevicePhase::kStarted) {
            return SnapshotFrame{};
        }
    }
    return snapshot_->Capture(request);
}

SnapshotInfo DeviceImpl::GetSnapshotInfo() const {
    return snapshot_->GetInfo();
}

OverlayInfo DeviceImpl::GetOverlayInfo() const {
    return region_overlay_->GetInfo();
}

}  // namespace

std::unique_ptr<DeviceMedia> CreateDeviceMediaImpl(
    const DeviceMediaOptions &options) {
    return std::unique_ptr<DeviceMedia>(new DeviceImpl(options));
}

}  // namespace device_internal
}  // namespace live_stream
