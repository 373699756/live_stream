#include "device_media_runtime.h"

#include "config.h"
#include "device_media_pipeline.h"
#include "media_channels.h"
#include "device_media_state.h"
#include "hisisdk/hisi_sdk.h"
#include "image_strategy.h"
#include "infra/log.h"
#include "media_config_codec.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace live_stream {
namespace device_media_internal {
namespace {

using media_internal::ParseVideoConfig;
using media_internal::ValidateImageConfig;

constexpr int kImageStrategyIntervalMs = 1000;

class DeviceMediaImpl : public IDeviceMedia {
public:
    explicit DeviceMediaImpl(const DeviceMediaOptions &device_options)
        : options_(device_options),
          pipeline_(device_options.default_config, device_options.sdk) {
        active_config_ = pipeline_.config();
        active_channels_ = BuildChannelsForConfig(active_config_);
        capabilities_ = pipeline_.GetCapabilities();
        pipeline_.SetFrameCallback(&DeviceMediaImpl::OnPipelineFrame, this);
    }

    ~DeviceMediaImpl() override { Release(); }

    bool Start() override;
    void Stop() override;
    bool IsStarted() const override;
    bool IsRestarting() const override;
    bool IsStreamStarted(StreamId stream_id) const override;
    Codec GetStreamCodec(StreamId stream_id) const override;
    bool SetFrameSink(FrameSink *sink) override;
    bool RequestKeyFrame(StreamId stream_id, KeyFrameRequestType reason) override;
    MediaCapabilities GetCapabilities() const override;
    MediaChannels GetChannels() const override;
    ImageStrategyStatus GetImageStrategyStatus() const override;

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
        ImageStrategyStatus *next_status) const;
    void StartImageStrategyLocked();
    void StopImageStrategy();
    void ImageStrategyLoop();
    bool ApplyPipelineConfig(const MediaPipelineConfig &config);

    DeviceMediaOptions options_;
    DeviceMediaPipeline pipeline_;
    MediaPipelineConfig active_config_;
    MediaChannels active_channels_;
    MediaCapabilities capabilities_;
    DeviceMediaState state_ = DeviceMediaState::kCreated;
    FrameSink *frame_sink_ = nullptr;
    ConfigJson image_config_ = ConfigJson::object();
    ImageStrategyStatus image_strategy_status_;
    mutable std::mutex mutex_;
    std::mutex pipeline_op_mutex_;
    bool video_config_attached_ = false;
    bool image_config_attached_ = false;
    bool system_initialized_ = false;
    std::thread image_strategy_thread_;
    bool image_strategy_running_ = false;
    bool image_strategy_stop_ = false;
};

bool DeviceMediaImpl::Prepare() {
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
        if (DeviceMediaPrepared(state_)) {
            return true;
        }
        if (!DeviceMediaCanPrepare(state_)) {
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
        if (DeviceMediaPrepared(state_)) {
            return true;
        }
        if (!DeviceMediaCanPrepare(state_)) {
            return false;
        }
        state_ = DeviceMediaState::kStopping;
    }

    pipeline_.SetConfig(startup_config);
    if (!pipeline_.InitSystem()) {
        const bool deinit_ok = pipeline_.DeinitSystem();
        std::lock_guard<std::mutex> lock(mutex_);
        if (deinit_ok) {
            state_ = DeviceMediaState::kDeinitialized;
            system_initialized_ = false;
        } else {
            state_ = DeviceMediaState::kFailed;
            system_initialized_ = true;
        }
        return false;
    }

    AttachedConfigs attached_now;
    if (!AttachConfigs(&attached_now)) {
        const bool deinit_ok = pipeline_.DeinitSystem();
        std::lock_guard<std::mutex> lock(mutex_);
        if (deinit_ok) {
            state_ = DeviceMediaState::kDeinitialized;
            system_initialized_ = false;
        } else {
            state_ = DeviceMediaState::kFailed;
            system_initialized_ = true;
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_config_ = startup_config;
        active_channels_ = BuildChannelsForConfig(active_config_);
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
        state_ = DeviceMediaState::kInitialized;
    }
    return true;
}

bool DeviceMediaImpl::AttachConfigs(AttachedConfigs *attached_now) {
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

void DeviceMediaImpl::Release() {
    StopImageStrategy();
    bool detach_video = false;
    bool detach_image = false;
    bool stop_pipeline = false;
    bool deinit_pipeline = false;
    {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ == DeviceMediaState::kStarted) {
                state_ = DeviceMediaState::kStopping;
                stop_pipeline = true;
                state_ = DeviceMediaState::kStopped;
            }
            if (state_ != DeviceMediaState::kDeinitialized &&
                state_ != DeviceMediaState::kCreated) {
                deinit_pipeline = true;
                state_ = DeviceMediaState::kStopping;
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
                state_ = DeviceMediaState::kDeinitialized;
                system_initialized_ = false;
            } else {
                state_ = DeviceMediaState::kFailed;
                system_initialized_ = true;
            }
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ != DeviceMediaState::kFailed) {
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

void DeviceMediaImpl::OnPipelineFrame(const EncodedFrame &frame, void *user) {
    if (user != nullptr) {
        static_cast<DeviceMediaImpl *>(user)->DispatchFrame(frame);
    }
}

void DeviceMediaImpl::DispatchFrame(const EncodedFrame &frame) {
    FrameSink *frame_sink = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ != DeviceMediaState::kStarted) {
            return;
        }
        frame_sink = frame_sink_;
    }
    if (frame_sink != nullptr) {
        (void)frame_sink->PushFrame(frame);
    }
}

ConfigResult DeviceMediaImpl::CheckVideoConfig(
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

ConfigResult DeviceMediaImpl::ApplyVideoConfig(const ConfigJson &value) {
    MediaPipelineConfig next_config;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ == DeviceMediaState::kStopping) {
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

ConfigResult DeviceMediaImpl::CheckImageConfig(
    const ConfigJson &value) const {
    return ValidateImageConfig(value, capabilities_.image, active_config_);
}

ConfigResult DeviceMediaImpl::ApplyImageConfig(const ConfigJson &value) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ != DeviceMediaState::kStarted) {
            image_config_ = value;
            return ConfigResult::Success();
        }
    }

    std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ != DeviceMediaState::kStarted) {
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

ConfigResult DeviceMediaImpl::CheckImageConfigForPipelineLocked(
    const MediaPipelineConfig &pipeline_config) const {
    if (!image_config_.is_object() || image_config_.empty()) {
        return ConfigResult::Success();
    }
    // Image features such as VPSS LDC depend on stream dimensions, so video
    // config changes must be checked against the current image config.
    return ValidateImageConfig(image_config_, capabilities_.image,
                               pipeline_config);
}

bool DeviceMediaImpl::ApplyImageConfigToPipeline(
    const ConfigJson &value) {
    if (!value.is_object() || value.empty()) {
        return true;
    }
    return pipeline_.ApplyImageConfig(value);
}

ConfigJson DeviceMediaImpl::BuildImageStrategyConfigLocked(
    const hisisdk::ExposureInfo &exposure,
    ImageStrategyStatus *next_status) const {
    return BuildImageStrategyConfig(image_config_, image_strategy_status_,
                                    exposure, next_status);
}

void DeviceMediaImpl::StartImageStrategyLocked() {
    if (image_strategy_running_) {
        return;
    }
    image_strategy_stop_ = false;
    image_strategy_running_ = true;
    image_strategy_thread_ =
        std::thread(&DeviceMediaImpl::ImageStrategyLoop, this);
}

void DeviceMediaImpl::StopImageStrategy() {
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
    image_strategy_status_.active = false;
}

void DeviceMediaImpl::ImageStrategyLoop() {
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
            image_strategy_status_.enabled = strategy_enabled;
            if (state_ != DeviceMediaState::kStarted || !strategy_enabled) {
                image_strategy_status_.active = false;
                continue;
            }
        }

        const hisisdk::ExposureInfo exposure = pipeline_.QueryExposureInfo();
        if (!exposure.valid) {
            std::lock_guard<std::mutex> guard(mutex_);
            if (image_strategy_stop_) {
                return;
            }
            image_strategy_status_.exposure_valid = false;
            continue;
        }

        ImageStrategyStatus next_status;
        ConfigJson adjusted;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (image_strategy_stop_ ||
                state_ != DeviceMediaState::kStarted ||
                !IsImageStrategyEnabled(image_config_)) {
                continue;
            }
            adjusted = BuildImageStrategyConfigLocked(exposure,
                                                      &next_status);
        }

        bool applied = false;
        {
            std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
            bool can_apply = false;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                can_apply = !image_strategy_stop_ &&
                            state_ == DeviceMediaState::kStarted &&
                            IsImageStrategyEnabled(image_config_);
            }
            if (can_apply) {
                applied = pipeline_.ApplyImageConfig(adjusted);
            }
        }
        if (applied) {
            std::lock_guard<std::mutex> guard(mutex_);
            image_strategy_status_ = next_status;
        }
    }
}

bool DeviceMediaImpl::ApplyPipelineConfig(
    const MediaPipelineConfig &config) {
    bool restart_stream = false;
    bool rebuild_system = false;
    DeviceMediaState state_before_change = DeviceMediaState::kCreated;
    MediaPipelineConfig config_before_change;
    ConfigJson image_config_before_change;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_ == DeviceMediaState::kStopping ||
            state_ == DeviceMediaState::kFailed) {
            return false;
        }
        state_before_change = state_;
        restart_stream = state_ == DeviceMediaState::kStarted;
        rebuild_system = system_initialized_;
        config_before_change = active_config_;
        image_config_before_change = image_config_;
        state_ = DeviceMediaState::kStopping;
    }

    if (restart_stream) {
        StopImageStrategy();
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

            device_config_applied = true;
            if (rebuild_system && !pipeline_.InitSystem()) {
                device_config_applied = false;
            } else if (restart_stream && !pipeline_.Start()) {
                device_config_applied = false;
            } else if (restart_stream &&
                       !ApplyImageConfigToPipeline(
                           image_config_before_change)) {
                device_config_applied = false;
            }
        }

        if (!device_config_applied && deinit_ok &&
            (restart_stream || rebuild_system)) {
            pipeline_.Stop();
            if (rebuild_system) {
                (void)pipeline_.DeinitSystem();
            }
            pipeline_.SetConfig(config_before_change);
            bool restored = true;
            if (rebuild_system && !pipeline_.InitSystem()) {
                restored = false;
            }
            if (restored && restart_stream && !pipeline_.Start()) {
                restored = false;
            }
            if (restored && restart_stream &&
                !ApplyImageConfigToPipeline(image_config_before_change)) {
                restored = false;
            }
            if (!restored) {
                pipeline_.Stop();
                if (rebuild_system) {
                    (void)pipeline_.DeinitSystem();
                }
                Error("device_media",
                      "restore media pipeline after config failure failed");
            } else {
                Error("device_media",
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
            state_ = restart_stream ? DeviceMediaState::kStarted
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
            state_ = restart_stream ? DeviceMediaState::kStarted
                                    : state_before_change;
            if (restart_stream) {
                StartImageStrategyLocked();
            }
        } else {
            if (rebuild_system) {
                system_initialized_ = true;
                state_ = DeviceMediaState::kFailed;
            } else if (restart_stream) {
                system_initialized_ = false;
                state_ = DeviceMediaState::kStopped;
            } else {
                system_initialized_ = false;
                state_ = state_before_change;
            }
        }
    }
    return false;
}

bool DeviceMediaImpl::Start() {
    bool need_init = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        need_init = DeviceMediaCanPrepare(state_);
    }
    if (need_init && !Prepare()) {
        return false;
    }

    ConfigJson image_config_before_change;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == DeviceMediaState::kStarted) {
            return true;
        }
        if (state_ == DeviceMediaState::kStopped) {
            state_ = DeviceMediaState::kInitialized;
        }
        if (state_ != DeviceMediaState::kInitialized) {
            return false;
        }
        state_ = DeviceMediaState::kStopping;
        image_config_before_change = image_config_;
    }

    bool ok = false;
    {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
        ok = pipeline_.Start();
        if (ok) {
            ok = ApplyImageConfigToPipeline(image_config_before_change);
        }
        if (!ok) {
            pipeline_.Stop();
        }
    }
    if (!ok) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = DeviceMediaState::kInitialized;
        }
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = DeviceMediaState::kStarted;
        StartImageStrategyLocked();
    }
    return true;
}

void DeviceMediaImpl::Stop() {
    StopImageStrategy();
    bool should_stop = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != DeviceMediaState::kStarted) {
            if (state_ == DeviceMediaState::kStopping) {
                state_ = DeviceMediaState::kStopped;
            }
            return;
        }

        state_ = DeviceMediaState::kStopping;
        should_stop = true;
    }

    if (should_stop) {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex_);
        pipeline_.Stop();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = DeviceMediaState::kStopped;
}

bool DeviceMediaImpl::IsStarted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == DeviceMediaState::kStarted;
}

bool DeviceMediaImpl::IsRestarting() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == DeviceMediaState::kStopping;
}

bool DeviceMediaImpl::IsStreamStarted(StreamId stream_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const VideoStreamConfig *stream =
        FindConfiguredStream(active_config_, stream_id);
    return state_ == DeviceMediaState::kStarted && stream != nullptr &&
           stream->enabled;
}

Codec DeviceMediaImpl::GetStreamCodec(StreamId stream_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const VideoStreamConfig *stream =
        FindConfiguredStream(active_config_, stream_id);
    if (stream != nullptr) {
        return stream->codec;
    }
    return Codec::kH264;
}

bool DeviceMediaImpl::SetFrameSink(FrameSink *sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_sink_ = sink;
    return true;
}

bool DeviceMediaImpl::RequestKeyFrame(StreamId stream_id,
                                      KeyFrameRequestType reason) {
    (void)reason;
    int32_t venc_channel = -1;
    hisisdk::IHisiSdk *sdk = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const MediaPipelineConfig config = active_config_;
        const VideoStreamConfig *stream =
            FindConfiguredStream(config, stream_id);
        if (state_ != DeviceMediaState::kStarted || stream == nullptr ||
            !stream->enabled) {
            return false;
        }
        venc_channel = VencChannelForStream(config, stream_id);
        sdk = options_.sdk != nullptr ? options_.sdk
                                      : &hisisdk::DefaultSdk();
    }
    return sdk->RequestIdr(venc_channel);
}

MediaCapabilities DeviceMediaImpl::GetCapabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capabilities_;
}

MediaChannels DeviceMediaImpl::GetChannels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!system_initialized_ || state_ == DeviceMediaState::kFailed) {
        return MediaChannels{};
    }
    return active_channels_;
}

ImageStrategyStatus DeviceMediaImpl::GetImageStrategyStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return image_strategy_status_;
}

}  // namespace

std::unique_ptr<IDeviceMedia> CreateDeviceMediaCore(
    const DeviceMediaOptions &options) {
    return std::unique_ptr<IDeviceMedia>(new DeviceMediaImpl(options));
}

}  // namespace device_media_internal
}  // namespace live_stream
