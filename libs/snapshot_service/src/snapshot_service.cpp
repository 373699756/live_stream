#include "snapshot_service.h"

#include "config_service.h"
#include "hisisdk/hisi_sdk.h"

#include <mutex>
#include <utility>

namespace live_stream {

namespace {

enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

bool IsValidConfig(const SnapshotConfig& config) {
    return config.snap_pipe >= 0 && config.snap_vpss_group >= 0 &&
           config.snap_vpss_channel >= 0 && config.jpeg_venc_channel >= 0 &&
           config.size.width > 0 && config.size.height > 0 &&
           config.frame_count > 0 && config.repeat_send_times > 0;
}

bool IsValidRequest(const CaptureRequest& request) {
    return request.timeout_ms > 0 && request.jpeg_quality > 0 &&
           request.jpeg_quality <= 100 &&
           request.stream_id == infra::StreamId::kSnapshot;
}

bool IsValidMedia(const MediaChannels& channels) {
    return channels.video_pipe >= 0 && channels.snap_pipe >= 0 &&
           channels.vpss.device >= 0 && channels.vpss.channel >= 0;
}

hisisdk::SnapshotConfig ToHisiSnapshotConfig(
    const SnapshotConfig& config,
    const CaptureRequest& request) {
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

SnapshotFrame ToSnapshotFrame(const hisisdk::JpegFrame& hisi_frame) {
    SnapshotFrame frame;
    frame.buffer = hisi_frame.buffer;
    frame.offset = hisi_frame.offset;
    frame.size = hisi_frame.size;
    frame.width = hisi_frame.width;
    frame.height = hisi_frame.height;
    frame.pts_us = hisi_frame.pts_us;
    return frame;
}

infra::Status ParseSnapshotRuntimeConfig(const ConfigJson& value,
                                         bool* enabled,
                                         uint32_t* jpeg_quality,
                                         uint32_t* timeout_ms) {
    if (!value.is_object() || enabled == nullptr || jpeg_quality == nullptr ||
        timeout_ms == nullptr) {
        return infra::Status::kInvalidParam;
    }
    const int quality = value.value("jpeg_quality", 90);
    const int timeout = value.value("timeout_ms", 3000);
    if (quality <= 0 || quality > 100 || timeout <= 0) {
        return infra::Status::kInvalidParam;
    }
    *enabled = value.value("enabled", true);
    *jpeg_quality = static_cast<uint32_t>(quality);
    *timeout_ms = static_cast<uint32_t>(timeout);
    return infra::Status::kOk;
}

}  // namespace

struct SnapshotService::Impl {
    explicit Impl(const SnapshotServiceOptions& service_options)
        : options(service_options),
          config(std::move(service_options.default_config)),
          sdk(service_options.sdk != nullptr ? service_options.sdk
                                             : &hisisdk::DefaultSdk()) {}

    SnapshotServiceOptions options;
    SnapshotConfig config;
    hisisdk::IHisiSdk* sdk = nullptr;
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
    bool config_callbacks_registered = false;

    void CleanupCaptureSession() {
        snap_pipe_enabled = false;
        snap_vpss_bound_venc = false;
        jpeg_venc_started = false;
        vi_bound_snap_vpss = false;
        snap_vpss_started = false;
        capturing = false;
    }

    infra::Status PrepareCaptureSession() {
        if (!media_bound) {
            return infra::Status::kBusy;
        }
        snap_vpss_started = true;
        vi_bound_snap_vpss = true;
        jpeg_venc_started = true;
        snap_vpss_bound_venc = true;
        snap_pipe_enabled = true;
        return infra::Status::kOk;
    }

    infra::Status VerifyConfig(const ConfigJson& value) const {
        bool next_enabled = true;
        uint32_t next_quality = 0;
        uint32_t next_timeout = 0;
        return ParseSnapshotRuntimeConfig(value, &next_enabled, &next_quality,
                                          &next_timeout);
    }

    infra::Status ApplyConfig(const ConfigJson& value) {
        bool next_enabled = true;
        uint32_t next_quality = 0;
        uint32_t next_timeout = 0;
        const infra::Status status = ParseSnapshotRuntimeConfig(
            value, &next_enabled, &next_quality, &next_timeout);
        if (status != infra::Status::kOk) {
            ++stats.config_apply_failed_count;
            return status;
        }
        enabled = next_enabled;
        default_jpeg_quality = next_quality;
        default_timeout_ms = next_timeout;
        ++stats.config_apply_count;
        return infra::Status::kOk;
    }
};

SnapshotService::SnapshotService() : SnapshotService(SnapshotServiceOptions{}) {}

SnapshotService::SnapshotService(const SnapshotConfig& config)
    : SnapshotService(SnapshotServiceOptions{config, nullptr, nullptr}) {}

SnapshotService::SnapshotService(const SnapshotServiceOptions& options)
    : impl_(new Impl(options)) {}

SnapshotService::~SnapshotService() {
    Deinit();
    delete impl_;
    impl_ = nullptr;
}

infra::Status SnapshotService::Init() {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!IsValidConfig(impl_->config)) {
        return infra::Status::kInvalidParam;
    }
    if (impl_->options.config_service != nullptr &&
        !impl_->config_callbacks_registered) {
        infra::Status status =
            impl_->options.config_service->RegisterVerify(
                "snapshot", [this](const ConfigJson& value) {
                    std::lock_guard<std::mutex> guard(impl_->mutex);
                    return impl_->VerifyConfig(value);
                });
        if (status != infra::Status::kOk) {
            return status;
        }
        status = impl_->options.config_service->RegisterApply(
            "snapshot", [this](const ConfigJson& value) {
                std::lock_guard<std::mutex> guard(impl_->mutex);
                return impl_->ApplyConfig(value);
            });
        if (status != infra::Status::kOk) {
            return status;
        }
        impl_->config_callbacks_registered = true;
    }
    if (impl_->state == ServiceState::kInitialized ||
        impl_->state == ServiceState::kStarted ||
        impl_->state == ServiceState::kStopped) {
        return infra::Status::kOk;
    }
    impl_->state = ServiceState::kInitialized;
    return infra::Status::kOk;
}

infra::Status SnapshotService::Start() {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == ServiceState::kStarted) {
        return infra::Status::kOk;
    }
    if (impl_->state == ServiceState::kStopped) {
        impl_->state = ServiceState::kInitialized;
    }
    if (impl_->state != ServiceState::kInitialized) {
        return infra::Status::kBusy;
    }
    if (!impl_->media_bound) {
        return infra::Status::kBusy;
    }
    impl_->state = ServiceState::kStarted;
    if (impl_->options.config_service != nullptr) {
        ConfigJson snapshot_config;
        if (impl_->options.config_service->GetValue(
                "snapshot", &snapshot_config) == infra::Status::kOk) {
            return impl_->ApplyConfig(snapshot_config);
        }
    }
    return infra::Status::kOk;
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

void SnapshotService::Deinit() {
    if (impl_ == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->CleanupCaptureSession();
    impl_->media_bound = false;
    if (impl_->state != ServiceState::kCreated) {
        impl_->state = ServiceState::kDeinitialized;
    }
}

const char* SnapshotService::Name() const {
    return StaticName();
}

const char* SnapshotService::StaticName() {
    return "snapshot_service";
}

infra::Status SnapshotService::BindMedia(const MediaChannels& channels) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == ServiceState::kStarted || impl_->capturing) {
        return infra::Status::kBusy;
    }
    if (!IsValidMedia(channels)) {
        return infra::Status::kInvalidParam;
    }
    impl_->media_channels = channels;
    impl_->config.snap_pipe = channels.snap_pipe;
    impl_->config.snap_vpss_group = channels.snap_pipe;
    impl_->media_bound = true;
    return infra::Status::kOk;
}

infra::Result<SnapshotFrame> SnapshotService::Capture(const CaptureRequest& request) {
    if (impl_ == nullptr) {
        return infra::Result<SnapshotFrame>::Fail(infra::Status::kInternalError);
    }
    CaptureRequest effective_request = request;
    SnapshotConfig capture_config;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->state != ServiceState::kStarted || impl_->capturing ||
            !impl_->enabled) {
            return infra::Result<SnapshotFrame>::Fail(infra::Status::kBusy);
        }
        if (!IsValidRequest(request)) {
            return infra::Result<SnapshotFrame>::Fail(
                infra::Status::kInvalidParam);
        }

        impl_->capturing = true;
        const CaptureRequest defaults;
        if (effective_request.jpeg_quality == defaults.jpeg_quality) {
            effective_request.jpeg_quality = impl_->default_jpeg_quality;
        }
        if (effective_request.timeout_ms == defaults.timeout_ms) {
            effective_request.timeout_ms = impl_->default_timeout_ms;
        }
        const infra::Status prepare_error = impl_->PrepareCaptureSession();
        if (prepare_error != infra::Status::kOk) {
            impl_->CleanupCaptureSession();
            impl_->capturing = false;
            ++impl_->stats.capture_failed_count;
            return infra::Result<SnapshotFrame>::Fail(prepare_error);
        }
        capture_config = impl_->config;
    }

    infra::Result<hisisdk::JpegFrame> hisi_frame =
        impl_->sdk->CaptureJpeg(
            ToHisiSnapshotConfig(capture_config, effective_request));

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->CleanupCaptureSession();
    impl_->capturing = false;
    if (!hisi_frame.IsOk()) {
        ++impl_->stats.capture_failed_count;
        return infra::Result<SnapshotFrame>::Fail(hisi_frame.status);
    }
    ++impl_->stats.capture_count;
    return infra::Result<SnapshotFrame>::Ok(ToSnapshotFrame(hisi_frame.value));
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
