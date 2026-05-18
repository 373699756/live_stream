#include "snapshot_service.h"

#include <mutex>
#include <utility>

#include "config_service.h"
#include "hisisdk/hisi_sdk.h"
#include "live_stream/json_utils.h"
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
           config.size.width > 0 && config.size.height > 0 &&
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
           channels.vpss.device >= 0 && channels.vpss.channel >= 0;
}

hisisdk::SnapshotConfig ToHisiSnapshotConfig(const SnapshotConfig &config,
                                             const CaptureRequest &request) {
    hisisdk::SnapshotConfig hisi_config;
    hisi_config.snap_pipe = config.snap_pipe;
    hisi_config.snap_vpss_group = config.snap_vpss_group;
    hisi_config.snap_vpss_channel = config.snap_vpss_channel;
    hisi_config.jpeg_venc_channel = config.jpeg_venc_channel;
    hisi_config.size = hisisdk::Size{config.size.width, config.size.height};
    hisi_config.frame_count = config.frame_count;
    hisi_config.repeat_send_times = config.repeat_send_times;
    hisi_config.timeout_ms = request.timeout_ms;
    hisi_config.jpeg_quality = request.jpeg_quality;
    hisi_config.load_ccm = config.load_ccm;
    hisi_config.zero_shutter_lag = config.zero_shutter_lag;
    return hisi_config;
}

SnapshotFrame ToSnapshotFrame(const hisisdk::JpegFrame &hisi_frame) {
    SnapshotFrame frame;
    frame.buffer = hisi_frame.buffer;
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
    return json_utils::Load(value, "enabled", enabled) &&
           json_utils::Load(value, "jpeg_quality", jpeg_quality, 1, 100) &&
           json_utils::Load(value, "timeout_ms", timeout_ms, 1, 0xffffffffU);
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
    bool snap_vpss_started = false;
    bool vi_bound_snap_vpss = false;
    bool jpeg_venc_started = false;
    bool snap_vpss_bound_venc = false;
    bool snap_pipe_enabled = false;
    MediaChannels media_channels{};
    SnapshotServiceStats stats;
    mutable std::mutex mutex;
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
            std::lock_guard<std::mutex> lock(mutex);
            detach = config_attached;
            CleanupCaptureSession();
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

    void CleanupCaptureSession() {
        snap_pipe_enabled = false;
        snap_vpss_bound_venc = false;
        jpeg_venc_started = false;
        vi_bound_snap_vpss = false;
        snap_vpss_started = false;
        capturing = false;
    }

    bool PrepareCaptureSession() {
        if (!media_bound) {
            return false;
        }
        snap_vpss_started = true;
        vi_bound_snap_vpss = true;
        jpeg_venc_started = true;
        snap_vpss_bound_venc = true;
        snap_pipe_enabled = true;
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
          SnapshotServiceOptions{config, nullptr, nullptr, nullptr}) {}

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
        if (impl_->options.media_service == nullptr) {
            return false;
        }
        const MediaChannels channels = impl_->options.media_service->GetChannels();
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
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->CleanupCaptureSession();
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
    impl_->config.snap_pipe = channels.snap_pipe;
    impl_->config.snap_vpss_group = channels.snap_pipe;
    impl_->media_bound = true;
    return true;
}

SnapshotFrame SnapshotService::Capture(const CaptureRequest &request) {
    if (impl_ == nullptr) {
        return SnapshotFrame{};
    }
    CaptureRequest effective_request = request;
    SnapshotConfig capture_config;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->state != ServiceState::kStarted || impl_->capturing ||
            !impl_->enabled) {
            return SnapshotFrame{};
        }
        if (!IsValidRequest(request)) {
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
            impl_->CleanupCaptureSession();
            impl_->capturing = false;
            ++impl_->stats.capture_failed_count;
            return SnapshotFrame{};
        }
        capture_config = impl_->config;
    }

    hisisdk::JpegFrame hisi_frame = impl_->sdk->CaptureJpeg(
        ToHisiSnapshotConfig(capture_config, effective_request));

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->CleanupCaptureSession();
    impl_->capturing = false;
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
