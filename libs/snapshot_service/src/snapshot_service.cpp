#include "snapshot_service.h"

#include <condition_variable>
#include <mutex>
#include <utility>

#include "config_service.h"
#include "hisisdk/hisi_sdk.h"
#include "json_utils.h"
#include "media_service.h"

namespace live_stream {

namespace {

enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

bool IsValidConfig(const SnapshotConfig &config) {
    return config.snap_pipe >= 0 && config.snap_vpss_group >= 0 &&
           config.snap_vpss_channel >= 0 && config.jpeg_venc_channel >= 0 &&
           config.frame_count > 0 && config.repeat_send_times > 0;
}

bool IsValidRequest(const CaptureRequest &request) {
    return request.timeout_ms > 0 && request.jpeg_quality > 0 &&
           request.jpeg_quality <= 100 &&
           (request.stream_id == StreamId::kMain ||
            request.stream_id == StreamId::kSub ||
            request.stream_id == StreamId::kSnapshot);
}

bool IsValidMedia(const MediaChannels &channels) {
    return channels.video_pipe >= 0 && channels.snap_pipe >= 0 &&
           channels.vpss.device >= 0 && channels.vpss.channel >= 0 &&
           channels.main_size.width > 0 && channels.main_size.height > 0;
}

bool IsSnapshotVencChannelAvailable(const SnapshotConfig &config,
                                    const MediaChannels &channels) {
    if (channels.venc.channel == config.jpeg_venc_channel) {
        return false;
    }
    if (channels.sub_venc.module == MppModule::kVenc &&
        channels.sub_venc.channel >= 0 &&
        channels.sub_venc.channel == config.jpeg_venc_channel) {
        return false;
    }
    return true;
}

SnapshotConfig BuildCaptureConfig(const SnapshotConfig &base_config,
                                  const MediaChannels &channels,
                                  StreamId stream_id) {
    SnapshotConfig capture_config = base_config;
    capture_config.snap_pipe = channels.snap_pipe;
    if (stream_id == StreamId::kSub && channels.sub_vpss.device >= 0 &&
        channels.sub_vpss.channel >= 0 && channels.sub_size.width > 0 &&
        channels.sub_size.height > 0) {
        capture_config.snap_vpss_group = channels.sub_vpss.device;
        capture_config.snap_vpss_channel = channels.sub_vpss.channel;
        capture_config.size = channels.sub_size;
        return capture_config;
    }
    capture_config.snap_vpss_group = channels.vpss.device;
    capture_config.snap_vpss_channel = channels.vpss.channel;
    capture_config.size = channels.main_size;
    return capture_config;
}

hisisdk::SnapshotConfig BuildSdkSnapshotConfig(
    const SnapshotConfig &config, const CaptureRequest &request) {
    hisisdk::SnapshotConfig sdk_config;
    sdk_config.snap_pipe = config.snap_pipe;
    sdk_config.snap_vpss_group = config.snap_vpss_group;
    sdk_config.snap_vpss_channel = config.snap_vpss_channel;
    sdk_config.jpeg_venc_channel = config.jpeg_venc_channel;
    sdk_config.size = hisisdk::Size{config.size.width, config.size.height};
    sdk_config.frame_count = config.frame_count;
    sdk_config.repeat_send_times = config.repeat_send_times;
    sdk_config.timeout_ms = request.timeout_ms;
    sdk_config.jpeg_quality = request.jpeg_quality;
    sdk_config.load_ccm = config.load_ccm;
    sdk_config.zero_shutter_lag = config.zero_shutter_lag;
    return sdk_config;
}

SnapshotFrame ToSnapshotFrame(const hisisdk::JpegFrame &hisi_frame) {
    SnapshotFrame frame;
    frame.buffer = VideoBufferRef(hisi_frame.buffer);
    frame.offset = hisi_frame.offset;
    frame.size = hisi_frame.size;
    frame.width = hisi_frame.width;
    frame.height = hisi_frame.height;
    frame.pts_us = hisi_frame.pts_us;
    return frame;
}

bool ParseSnapshotRuntimeConfig(const ConfigJson &value, bool *enabled,
                                uint32_t *jpeg_quality, uint32_t *timeout_ms) {
    if (!value.is_object() || enabled == nullptr || jpeg_quality == nullptr ||
        timeout_ms == nullptr) {
        return false;
    }
    return json_utils::ReadField(value, "enabled", enabled) &&
           json_utils::ReadField(value, "jpeg_quality", jpeg_quality, 1, 100) &&
           json_utils::ReadField(value, "timeout_ms", timeout_ms, 1, 0xffffffffU);
}

}  // namespace

struct SnapshotService::Impl {
    explicit Impl(const SnapshotServiceOptions &service_options)
        : options(service_options),
          config(std::move(service_options.default_config)),
          sdk(service_options.sdk != nullptr ? service_options.sdk
                                             : &hisisdk::DefaultSdk()) {}

    SnapshotServiceOptions options;
    SnapshotConfig config;
    hisisdk::IHisiSdk *sdk = nullptr;
    ServiceState state = ServiceState::kCreated;
    bool media_bound = false;
    bool capturing = false;
    bool enabled = true;
    uint32_t default_jpeg_quality = 90;
    uint32_t default_timeout_ms = 3000;
    MediaChannels media_channels;
    SnapshotServiceStats stats;
    mutable std::mutex mutex;
    std::condition_variable capture_idle;
    bool config_attached = false;

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!IsValidConfig(config)) {
            return false;
        }
        if (options.config_service != nullptr && !config_attached) {
            ConfigAttachment attachment;
            attachment.validate = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex);
                return VerifyConfig(value)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "invalid snapshot config");
            };
            attachment.apply = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex);
                return ApplyConfig(value)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "apply snapshot config failed");
            };
            if (!options.config_service->AttachConfig("snapshot", attachment)) {
                return false;
            }
            config_attached = true;
        }
        if (state == ServiceState::kInitialized ||
            state == ServiceState::kStarted || state == ServiceState::kStopped) {
            return true;
        }
        state = ServiceState::kInitialized;
        return true;
    }

    void Release() {
        bool detach = false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            detach = config_attached;
            WaitCaptureIdleLocked(lock);
            media_bound = false;
            config_attached = false;
            if (state != ServiceState::kCreated) {
                state = ServiceState::kDeinitialized;
            }
        }
        if (detach && options.config_service != nullptr) {
            static_cast<void>(options.config_service->DetachConfig("snapshot"));
        }
    }

    void FinishCaptureSession() {
        capturing = false;
        capture_idle.notify_all();
    }

    void WaitCaptureIdleLocked(std::unique_lock<std::mutex> &lock) {
        while (capturing) {
            capture_idle.wait(lock);
        }
    }

    bool PrepareCaptureSession() {
        if (!media_bound) {
            return false;
        }
        if (!IsSnapshotVencChannelAvailable(config, media_channels)) {
            return false;
        }
        return true;
    }

    bool VerifyConfig(const ConfigJson &value) const {
        bool next_enabled = true;
        uint32_t next_quality = 0;
        uint32_t next_timeout = 0;
        return ParseSnapshotRuntimeConfig(value, &next_enabled, &next_quality,
                                          &next_timeout);
    }

    bool ApplyConfig(const ConfigJson &value) {
        bool next_enabled = true;
        uint32_t next_quality = 0;
        uint32_t next_timeout = 0;
        ParseSnapshotRuntimeConfig(value, &next_enabled, &next_quality,
                                   &next_timeout);
        enabled = next_enabled;
        default_jpeg_quality = next_quality;
        default_timeout_ms = next_timeout;
        ++stats.config_apply_count;
        return true;
    }
};

SnapshotService::SnapshotService()
    : SnapshotService(SnapshotServiceOptions{}) {}

SnapshotService::SnapshotService(const SnapshotConfig &config)
    : SnapshotService(
          SnapshotServiceOptions{config, nullptr, nullptr, MediaChannels{},
                                 nullptr}) {}

SnapshotService::SnapshotService(const SnapshotServiceOptions &options)
    : impl_(new Impl(options)) {}

SnapshotService::~SnapshotService() {
    if (impl_ != nullptr) {
        impl_->Release();
        delete impl_;
        impl_ = nullptr;
    }
}

bool SnapshotService::Start() {
    if (impl_ == nullptr) {
        return false;
    }
    bool need_init = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        need_init = impl_->state == ServiceState::kCreated ||
                    impl_->state == ServiceState::kDeinitialized;
    }
    if (need_init && !impl_->Prepare()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == ServiceState::kStarted) {
        return true;
    }
    if (impl_->state == ServiceState::kStopped) {
        impl_->state = ServiceState::kInitialized;
    }
    if (impl_->state != ServiceState::kInitialized) {
        return false;
    }
    if (!impl_->media_bound) {
        const MediaChannels channels = impl_->options.media_channels;
        if (!IsValidMedia(channels)) {
            return false;
        }
        impl_->media_channels = channels;
        impl_->media_bound = true;
    }
    impl_->state = ServiceState::kStarted;
    if (impl_->options.config_service != nullptr) {
        ConfigJson snapshot_config =
            impl_->options.config_service->GetValue("snapshot");
        if (snapshot_config.is_object()) {
            return impl_->ApplyConfig(snapshot_config);
        }
    }
    return true;
}

void SnapshotService::Stop() {
    if (impl_ == nullptr) {
        return;
    }
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->WaitCaptureIdleLocked(lock);
    if (impl_->state == ServiceState::kStarted) {
        impl_->state = ServiceState::kStopped;
    }
}

const char *SnapshotService::StaticName() { return "snapshot_service"; }

bool SnapshotService::BindMedia(const MediaChannels &channels) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == ServiceState::kStarted || impl_->capturing) {
        return false;
    }
    if (!IsValidMedia(channels)) {
        return false;
    }
    impl_->media_channels = channels;
    if (!IsSnapshotVencChannelAvailable(impl_->config, channels)) {
        return false;
    }
    impl_->media_bound = true;
    return true;
}

SnapshotFrame SnapshotService::Capture(const CaptureRequest &request) {
    if (impl_ == nullptr) {
        return SnapshotFrame{};
    }
    CaptureRequest effective_request = request;
    SnapshotConfig capture_config;
    MediaChannels channels;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->state != ServiceState::kStarted || impl_->capturing ||
            !impl_->enabled || impl_->sdk == nullptr) {
            return SnapshotFrame{};
        }
        if (!IsValidRequest(request)) {
            return SnapshotFrame{};
        }

        if (impl_->options.media_service != nullptr &&
            !impl_->options.media_service->IsStarted()) {
            ++impl_->stats.capture_failed_count;
            return SnapshotFrame{};
        }
        channels = impl_->media_channels;
        if (!IsValidMedia(channels)) {
            ++impl_->stats.capture_failed_count;
            return SnapshotFrame{};
        }
        if (!IsSnapshotVencChannelAvailable(impl_->config, channels)) {
            ++impl_->stats.capture_failed_count;
            return SnapshotFrame{};
        }

        impl_->capturing = true;
        const CaptureRequest defaults;
        if (effective_request.jpeg_quality == defaults.jpeg_quality) {
            effective_request.jpeg_quality = impl_->default_jpeg_quality;
        }
        if (effective_request.timeout_ms == defaults.timeout_ms) {
            effective_request.timeout_ms = impl_->default_timeout_ms;
        }
        if (!impl_->PrepareCaptureSession()) {
            impl_->FinishCaptureSession();
            ++impl_->stats.capture_failed_count;
            return SnapshotFrame{};
        }
        capture_config = BuildCaptureConfig(
            impl_->config, impl_->media_channels, effective_request.stream_id);
        if (capture_config.size.width == 0 ||
            capture_config.size.height == 0) {
            impl_->FinishCaptureSession();
            ++impl_->stats.capture_failed_count;
            return SnapshotFrame{};
        }
    }

    hisisdk::JpegFrame hisi_frame = impl_->sdk->CaptureJpeg(
        BuildSdkSnapshotConfig(capture_config, effective_request));

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->FinishCaptureSession();
    if (!hisi_frame.buffer || hisi_frame.size == 0) {
        ++impl_->stats.capture_failed_count;
        return SnapshotFrame{};
    }
    ++impl_->stats.capture_count;
    return ToSnapshotFrame(hisi_frame);
}

bool SnapshotService::IsCapturing() const {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->capturing;
}

SnapshotServiceStats SnapshotService::GetStats() const {
    if (impl_ == nullptr) {
        return SnapshotServiceStats{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    SnapshotServiceStats stats = impl_->stats;
    stats.jpeg_quality = impl_->default_jpeg_quality;
    stats.timeout_ms = impl_->default_timeout_ms;
    stats.enabled = impl_->enabled;
    stats.capturing = impl_->capturing;
    return stats;
}

}  // namespace live_stream
