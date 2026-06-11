#include "device_impl.h"

#include "config.h"
#include "hardware_pipeline.h"
#include "media_channels.h"
#include "hisisdk/hisi_sdk.h"
#include "image_strategy.h"
#include "infra/log.h"
#include "media_config_codec.h"
#include "region_overlay.h"
#include "snapshot_capture.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace live_stream {
namespace device_internal {
namespace {

using media_internal::ParseVideoConfig;
using media_internal::ValidateImageConfig;

constexpr int kImageStrategyIntervalMs = 1000;

enum class LifecycleState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopping,
    kStopped,
    kDeinitialized,
    kFailed,
};

bool CanPrepare(LifecycleState state) {
    return state == LifecycleState::kCreated ||
           state == LifecycleState::kDeinitialized;
}

bool IsPrepared(LifecycleState state) {
    return state == LifecycleState::kInitialized ||
           state == LifecycleState::kStarted ||
           state == LifecycleState::kStopped;
}

class DeviceImpl : public DeviceMedia {
public:
    explicit DeviceImpl(const DeviceMediaOptions &device_options)
        : options_(device_options),
          pipeline_(device_options.default_config, device_options.sdk) {
        active_config_ = pipeline_.config();
        active_channels_ = BuildChannelsForConfig(active_config_);
        capabilities_ = pipeline_.GetCapabilities();
        pipeline_.SetFrameCallback(&DeviceImpl::OnPipelineFrame, this);

        SnapshotConfig snapshot_config;
        snapshot_config.jpeg_venc_channel = device_options.snapshot_venc_channel;
        SnapshotCaptureOptions snapshot_options;
        snapshot_options.default_config = snapshot_config;
        snapshot_options.config = device_options.config;
        snapshot_options.media_channels = active_channels_;
        snapshot_options.sdk = device_options.sdk;
        snapshot_capture_.reset(new SnapshotCapture(snapshot_options));

        RegionOverlayOptions overlay_options;
        overlay_options.config = device_options.config;
        overlay_options.media_channels = active_channels_;
        overlay_options.sdk = device_options.sdk;
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
    bool AttachConfigs(AttachedConfigs *attached_now);
    void Release();
    static void OnPipelineFrame(const EncodedFrame &frame, void *user);
    void DispatchFrame(const EncodedFrame &frame);
    ConfigResult CheckVideoConfig(const ConfigJson &value) const;
    ConfigResult ApplyVideoConfig(const ConfigJson &value);
    ConfigResult CheckImageConfig(const ConfigJson &value) const;
    ConfigResult ApplyImageConfig(const ConfigJson &value);
    ConfigResult CheckImageConfigForPipelineLocked(
        const MediaPipelineConfig &pipeline_config) const;
    bool ApplyImageConfigToPipeline(const ConfigJson &value);
    ConfigJson BuildImageStrategyConfigLocked(
        const hisisdk::ExposureInfo &exposure,
        ImageInfo *next_info) const;
    void StartImageStrategyLocked();
    void StopImageStrategy();
    void ImageStrategyLoop();
    bool ApplyPipelineConfig(const MediaPipelineConfig &config);
    bool BindDeviceFeatures(const MediaChannels &channels);
    bool StartDeviceFeatures();
    void StopDeviceFeatures();
    void ReleaseDeviceFeatures();

    DeviceMediaOptions options_;
    HardwarePipeline pipeline_;
    MediaPipelineConfig active_config_;
    MediaChannels active_channels_;
    MediaCapabilities capabilities_;
    LifecycleState state_ = LifecycleState::kCreated;
    FrameSink *frame_sink_ = nullptr;
    ConfigJson image_config_ = ConfigJson::object();
    ImageInfo image_info_;
    std::unique_ptr<SnapshotCapture> snapshot_capture_;
    std::unique_ptr<RegionOverlay> region_overlay_;
    mutable std::mutex mutex_;
    std::mutex pipeline_op_mutex_;
    bool video_config_attached_ = false;
    bool image_config_attached_ = false;
    bool system_initialized_ = false;
    std::thread image_strategy_thread_;
    bool image_strategy_running_ = false;
    bool image_strategy_stop_ = false;
};

bool DeviceImpl::Prepare() {
    ConfigJson video_config;
    ConfigJson next_image_config;
    if (options_.config != nullptr) {
        video_config = options_.config->GetValue("video");
        next_image_config = options_.config->GetValue("image");
    }

    MediaPipelineConfig startup_config;
    MediaCapabilities capabilities_snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (IsPrepared(state_)) {
            return true;
        }
        if (!CanPrepare(state_)) {
            return false;
        }
        startup_config = active_config_;
        capabilities_snapshot = capabilities_;
    }

    if (video_config.is_object()) {
        const ConfigResult result = ParseVideoConfig(
            video_config, startup_config, capabilities_snapshot,
            &startup_config);
        if (!result.ok) {
            return false;
        }
    }

    const bool has_image_config = next_image_config.is_object();
    if (has_image_config) {
        const ConfigResult result = ValidateImageConfig(
            next_image_config, capabilities_snapshot.image, startup_config);
        if (!result.ok) {
            return false;
        }
    }

    std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (IsPrepared(state_)) {
            return true;
        }
        if (!CanPrepare(state_)) {
            return false;
        }
        state_ = LifecycleState::kStopping;
    }

    pipeline_.SetConfig(startup_config);
    if (!pipeline_.InitSystem()) {
        const bool deinit_ok = pipeline_.DeinitSystem();
        std::lock_guard<std::mutex> lock(mutex_);
        if (deinit_ok) {
            state_ = LifecycleState::kDeinitialized;
            system_initialized_ = false;
        } else {
            state_ = LifecycleState::kFailed;
            system_initialized_ = true;
        }
        return false;
    }

    const MediaChannels startup_channels = BuildChannelsForConfig(startup_config);
    if (!BindDeviceFeatures(startup_channels)) {
        const bool deinit_ok = pipeline_.DeinitSystem();
        std::lock_guard<std::mutex> lock(mutex_);
        if (deinit_ok) {
            state_ = LifecycleState::kDeinitialized;
            system_initialized_ = false;
        } else {
            state_ = LifecycleState::kFailed;
            system_initialized_ = true;
        }
        return false;
    }

    AttachedConfigs attached_now;
    if (!AttachConfigs(&attached_now)) {
        const bool deinit_ok = pipeline_.DeinitSystem();
        std::lock_guard<std::mutex> lock(mutex_);
        if (deinit_ok) {
            state_ = LifecycleState::kDeinitialized;
            system_initialized_ = false;
        } else {
            state_ = LifecycleState::kFailed;
            system_initialized_ = true;
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_config_ = startup_config;
        active_channels_ = startup_channels;
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
        state_ = LifecycleState::kInitialized;
    }
    return true;
}

bool DeviceImpl::AttachConfigs(AttachedConfigs *attached_now) {
    if (attached_now == nullptr) {
        return false;
    }
    *attached_now = AttachedConfigs{};
    if (options_.config == nullptr) {
        return true;
    }

    bool need_video_attach = false;
    bool need_image_attach = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        need_video_attach = !video_config_attached_;
        need_image_attach = !image_config_attached_;
    }

    if (need_video_attach) {
        ConfigAttachment attachment;
        attachment.validate = [this](const ConfigJson &value) {
            std::lock_guard<std::mutex> guard(mutex_);
            return CheckVideoConfig(value);
        };
        attachment.apply = [this](const ConfigJson &value) {
            return ApplyVideoConfig(value);
        };
        if (!options_.config->AttachConfig("video", attachment)) {
            return false;
        }
        attached_now->video = true;
    }

    if (need_image_attach) {
        ConfigAttachment attachment;
        attachment.validate = [this](const ConfigJson &value) {
            std::lock_guard<std::mutex> guard(mutex_);
            return CheckImageConfig(value);
        };
        attachment.apply = [this](const ConfigJson &value) {
            return ApplyImageConfig(value);
        };
        if (!options_.config->AttachConfig("image", attachment)) {
            if (attached_now->video) {
                (void)options_.config->DetachConfig("video");
            }
            *attached_now = AttachedConfigs{};
            return false;
        }
        attached_now->image = true;
    }
    return true;
}

bool DeviceImpl::BindDeviceFeatures(const MediaChannels &channels) {
    if (snapshot_capture_ != nullptr &&
        !snapshot_capture_->BindMedia(channels)) {
        return false;
    }
    if (region_overlay_ != nullptr && !region_overlay_->BindMedia(channels)) {
        return false;
    }
    return true;
}

bool DeviceImpl::StartDeviceFeatures() {
    if (snapshot_capture_ != nullptr && !snapshot_capture_->Start()) {
        return false;
    }
    if (region_overlay_ != nullptr && !region_overlay_->Start()) {
        if (snapshot_capture_ != nullptr) {
            snapshot_capture_->Stop();
        }
        return false;
    }
    return true;
}

void DeviceImpl::StopDeviceFeatures() {
    if (region_overlay_ != nullptr) {
        region_overlay_->Stop();
    }
    if (snapshot_capture_ != nullptr) {
        snapshot_capture_->Stop();
    }
}

void DeviceImpl::ReleaseDeviceFeatures() {
    if (region_overlay_ != nullptr) {
        region_overlay_->Release();
    }
    if (snapshot_capture_ != nullptr) {
        snapshot_capture_->Release();
    }
}

void DeviceImpl::Release() {
    StopImageStrategy();
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
            if (state_ == LifecycleState::kStarted) {
                state_ = LifecycleState::kStopping;
                stop_pipeline = true;
                state_ = LifecycleState::kStopped;
            }
            if (state_ != LifecycleState::kDeinitialized &&
                state_ != LifecycleState::kCreated) {
                deinit_pipeline = true;
                state_ = LifecycleState::kStopping;
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
                state_ = LifecycleState::kDeinitialized;
                system_initialized_ = false;
            } else {
                state_ = LifecycleState::kFailed;
                system_initialized_ = true;
            }
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ != LifecycleState::kFailed) {
                system_initialized_ = false;
            }
        }
    }

    if (options_.config != nullptr) {
        if (detach_video) {
            (void)options_.config->DetachConfig("video");
        }
        if (detach_image) {
            (void)options_.config->DetachConfig("image");
        }
    }
}

void DeviceImpl::OnPipelineFrame(const EncodedFrame &frame, void *user) {
    if (user != nullptr) {
        static_cast<DeviceImpl *>(user)->DispatchFrame(frame);
    }
}

void DeviceImpl::DispatchFrame(const EncodedFrame &frame) {
    FrameSink *frame_sink = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ != LifecycleState::kStarted) {
            return;
        }
        frame_sink = frame_sink_;
    }
    if (frame_sink != nullptr) {
        (void)frame_sink->PushFrame(frame);
    }
}

ConfigResult DeviceImpl::CheckVideoConfig(
    const ConfigJson &value) const {
    MediaPipelineConfig parsed;
    ConfigResult result =
        ParseVideoConfig(value, active_config_, capabilities_, &parsed);
    if (!result.ok) {
        return result;
    }
    if (!IsValidSnapshotVencChannel(parsed)) {
        return ConfigResult::Failure("streams",
                                     "snapshot VENC channel conflicts");
    }
    return CheckImageConfigForPipelineLocked(parsed);
}

ConfigResult DeviceImpl::ApplyVideoConfig(const ConfigJson &value) {
    MediaPipelineConfig next_config;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ == LifecycleState::kStopping) {
            return ConfigResult::Failure("", "device media busy");
        }
        const ConfigResult result = ParseVideoConfig(
            value, active_config_, capabilities_, &next_config);
        if (!result.ok) {
            return result;
        }
        if (!IsValidSnapshotVencChannel(next_config)) {
            return ConfigResult::Failure(
                "streams", "snapshot VENC channel conflicts");
        }
        const ConfigResult image_result =
            CheckImageConfigForPipelineLocked(next_config);
        if (!image_result.ok) {
            return image_result;
        }
    }
    if (!ApplyPipelineConfig(next_config)) {
        return ConfigResult::Failure("streams.main", "apply failed");
    }
    return ConfigResult::Success();
}

ConfigResult DeviceImpl::CheckImageConfig(
    const ConfigJson &value) const {
    return ValidateImageConfig(value, capabilities_.image, active_config_);
}

ConfigResult DeviceImpl::ApplyImageConfig(const ConfigJson &value) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ != LifecycleState::kStarted) {
            image_config_ = value;
            return ConfigResult::Success();
        }
    }

    std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ != LifecycleState::kStarted) {
            image_config_ = value;
            return ConfigResult::Success();
        }
    }
    if (!pipeline_.ApplyImageConfig(value)) {
        return ConfigResult::Failure("image", "apply failed");
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        image_config_ = value;
    }
    return ConfigResult::Success();
}

ConfigResult DeviceImpl::CheckImageConfigForPipelineLocked(
    const MediaPipelineConfig &pipeline_config) const {
    if (!image_config_.is_object() || image_config_.empty()) {
        return ConfigResult::Success();
    }
    // Image features such as VPSS LDC depend on stream dimensions, so video
    // config changes must be checked against the current image config.
    return ValidateImageConfig(image_config_, capabilities_.image,
                               pipeline_config);
}

bool DeviceImpl::ApplyImageConfigToPipeline(
    const ConfigJson &value) {
    if (!value.is_object() || value.empty()) {
        return true;
    }
    return pipeline_.ApplyImageConfig(value);
}

ConfigJson DeviceImpl::BuildImageStrategyConfigLocked(
    const hisisdk::ExposureInfo &exposure,
    ImageInfo *next_info) const {
    return BuildImageStrategyConfig(image_config_, image_info_,
                                    exposure, next_info);
}

void DeviceImpl::StartImageStrategyLocked() {
    if (image_strategy_running_) {
        return;
    }
    image_strategy_stop_ = false;
    image_strategy_running_ = true;
    image_strategy_thread_ =
        std::thread(&DeviceImpl::ImageStrategyLoop, this);
}

void DeviceImpl::StopImageStrategy() {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        image_strategy_stop_ = true;
    }
    if (image_strategy_thread_.joinable()) {
        image_strategy_thread_.join();
    }
    std::lock_guard<std::mutex> guard(mutex_);
    image_strategy_running_ = false;
    image_strategy_stop_ = false;
    image_info_.active = false;
}

void DeviceImpl::ImageStrategyLoop() {
    while (true) {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (image_strategy_stop_) {
                return;
            }
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kImageStrategyIntervalMs));

        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (image_strategy_stop_) {
                return;
            }
            const bool strategy_enabled =
                IsImageStrategyEnabled(image_config_);
            image_info_.enabled = strategy_enabled;
            if (state_ != LifecycleState::kStarted || !strategy_enabled) {
                image_info_.active = false;
                continue;
            }
        }

        const hisisdk::ExposureInfo exposure = pipeline_.QueryExposureInfo();
        if (!exposure.valid) {
            std::lock_guard<std::mutex> guard(mutex_);
            if (image_strategy_stop_) {
                return;
            }
            image_info_.exposure_valid = false;
            continue;
        }

        ImageInfo next_info;
        ConfigJson adjusted;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (image_strategy_stop_ ||
                state_ != LifecycleState::kStarted ||
                !IsImageStrategyEnabled(image_config_)) {
                continue;
            }
            adjusted = BuildImageStrategyConfigLocked(exposure,
                                                      &next_info);
        }

        bool applied = false;
        {
            std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
            bool can_apply = false;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                can_apply = !image_strategy_stop_ &&
                            state_ == LifecycleState::kStarted &&
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
    bool restart_stream = false;
    bool rebuild_system = false;
    LifecycleState state_before_change = LifecycleState::kCreated;
    MediaPipelineConfig config_before_change;
    ConfigJson image_config_before_change;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ == LifecycleState::kStopping ||
            state_ == LifecycleState::kFailed) {
            return false;
        }
        state_before_change = state_;
        restart_stream = state_ == LifecycleState::kStarted;
        rebuild_system = system_initialized_;
        config_before_change = active_config_;
        image_config_before_change = image_config_;
        state_ = LifecycleState::kStopping;
    }

    if (restart_stream) {
        StopImageStrategy();
        StopDeviceFeatures();
    }

    bool device_config_applied = false;
    bool old_config_restored = false;
    {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
        if (restart_stream) {
            pipeline_.Stop();
        }
        bool deinit_ok = true;
        if (rebuild_system) {
            deinit_ok = pipeline_.DeinitSystem();
        }

        if (!deinit_ok) {
            device_config_applied = false;
        } else {
            pipeline_.SetConfig(config);
            const MediaChannels next_channels = BuildChannelsForConfig(config);

            device_config_applied = true;
            if (rebuild_system && !pipeline_.InitSystem()) {
                device_config_applied = false;
            } else if (!BindDeviceFeatures(next_channels)) {
                device_config_applied = false;
            } else if (restart_stream && !pipeline_.Start()) {
                device_config_applied = false;
            } else if (restart_stream &&
                       !ApplyImageConfigToPipeline(
                           image_config_before_change)) {
                device_config_applied = false;
            } else if (restart_stream && !StartDeviceFeatures()) {
                device_config_applied = false;
            }
        }

        if (!device_config_applied && deinit_ok &&
            (restart_stream || rebuild_system)) {
            StopDeviceFeatures();
            pipeline_.Stop();
            if (rebuild_system) {
                (void)pipeline_.DeinitSystem();
            }
            pipeline_.SetConfig(config_before_change);
            const MediaChannels previous_channels =
                BuildChannelsForConfig(config_before_change);
            bool restored = true;
            if (rebuild_system && !pipeline_.InitSystem()) {
                restored = false;
            }
            if (restored && !BindDeviceFeatures(previous_channels)) {
                restored = false;
            }
            if (restored && restart_stream && !pipeline_.Start()) {
                restored = false;
            }
            if (restored && restart_stream &&
                !ApplyImageConfigToPipeline(image_config_before_change)) {
                restored = false;
            }
            if (restored && restart_stream && !StartDeviceFeatures()) {
                restored = false;
            }
            if (!restored) {
                StopDeviceFeatures();
                pipeline_.Stop();
                if (rebuild_system) {
                    (void)pipeline_.DeinitSystem();
                }
                Error("device",
                      "restore media pipeline after config failure failed");
            } else {
                Error("device",
                      "media config apply failed, restored previous pipeline");
            }
            old_config_restored = restored;
        }
    }

    if (device_config_applied) {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            active_config_ = config;
            active_channels_ = BuildChannelsForConfig(active_config_);
            system_initialized_ = rebuild_system;
            state_ = restart_stream ? LifecycleState::kStarted
                                    : state_before_change;
            if (restart_stream) {
                StartImageStrategyLocked();
            }
        }
        return true;
    }

    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (old_config_restored) {
            active_config_ = config_before_change;
            active_channels_ = BuildChannelsForConfig(active_config_);
            system_initialized_ = rebuild_system;
            state_ = restart_stream ? LifecycleState::kStarted
                                    : state_before_change;
            if (restart_stream) {
                StartImageStrategyLocked();
            }
        } else {
            if (rebuild_system) {
                system_initialized_ = true;
                state_ = LifecycleState::kFailed;
            } else if (restart_stream) {
                system_initialized_ = false;
                state_ = LifecycleState::kStopped;
            } else {
                system_initialized_ = false;
                state_ = state_before_change;
            }
        }
    }
    return false;
}

bool DeviceImpl::Start() {
    bool need_init = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        need_init = CanPrepare(state_);
    }
    if (need_init && !Prepare()) {
        return false;
    }

    ConfigJson image_config_before_change;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == LifecycleState::kStarted) {
            return true;
        }
        if (state_ == LifecycleState::kStopped) {
            state_ = LifecycleState::kInitialized;
        }
        if (state_ != LifecycleState::kInitialized) {
            return false;
        }
        state_ = LifecycleState::kStopping;
        image_config_before_change = image_config_;
    }

    bool ok = false;
    {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
        ok = pipeline_.Start();
        if (ok) {
            ok = ApplyImageConfigToPipeline(image_config_before_change);
        }
        if (ok) {
            ok = StartDeviceFeatures();
        }
        if (!ok) {
            StopDeviceFeatures();
            pipeline_.Stop();
        }
    }
    if (!ok) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = LifecycleState::kInitialized;
        }
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = LifecycleState::kStarted;
        StartImageStrategyLocked();
    }
    return true;
}

void DeviceImpl::Stop() {
    StopImageStrategy();
    StopDeviceFeatures();
    bool should_stop = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != LifecycleState::kStarted) {
            if (state_ == LifecycleState::kStopping) {
                state_ = LifecycleState::kStopped;
            }
            return;
        }

        state_ = LifecycleState::kStopping;
        should_stop = true;
    }

    if (should_stop) {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
        pipeline_.Stop();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = LifecycleState::kStopped;
}

bool DeviceImpl::IsStarted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == LifecycleState::kStarted;
}

bool DeviceImpl::IsRestarting() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == LifecycleState::kStopping;
}

bool DeviceImpl::IsStreamStarted(StreamId stream_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const VideoStreamConfig *stream =
        FindConfiguredStream(active_config_, stream_id);
    return state_ == LifecycleState::kStarted && stream != nullptr &&
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
    hisisdk::IHisiSdk *sdk = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const MediaPipelineConfig config = active_config_;
        const VideoStreamConfig *stream =
            FindConfiguredStream(config, stream_id);
        if (state_ != LifecycleState::kStarted || stream == nullptr ||
            !stream->enabled) {
            return false;
        }
        venc_channel = VencChannelForStream(config, stream_id);
        sdk = options_.sdk != nullptr ? options_.sdk
                                      : &hisisdk::DefaultSdk();
    }
    return sdk->RequestIdr(venc_channel);
}

MediaCapabilities DeviceImpl::GetCapabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capabilities_;
}

MediaChannels DeviceImpl::GetChannels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!system_initialized_ || state_ == LifecycleState::kFailed) {
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
    SnapshotCapture *snapshot_capture = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != LifecycleState::kStarted) {
            return SnapshotFrame{};
        }
        snapshot_capture = snapshot_capture_.get();
    }
    if (snapshot_capture == nullptr) {
        return SnapshotFrame{};
    }
    return snapshot_capture->Capture(request);
}

SnapshotInfo DeviceImpl::GetSnapshotInfo() const {
    if (snapshot_capture_ == nullptr) {
        return SnapshotInfo{};
    }
    return snapshot_capture_->GetInfo();
}

OverlayInfo DeviceImpl::GetOverlayInfo() const {
    if (region_overlay_ == nullptr) {
        return OverlayInfo{};
    }
    return region_overlay_->GetInfo();
}

}  // namespace

std::unique_ptr<DeviceMedia> CreateDeviceMediaCore(
    const DeviceMediaOptions &options) {
    return std::unique_ptr<DeviceMedia>(new DeviceImpl(options));
}

}  // namespace device_internal
}  // namespace live_stream
