#include "snapshot_service.h"

#include "hisisdk/hisi_sdk.h"

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

}  // namespace

struct SnapshotService::Impl {
    explicit Impl(SnapshotConfig service_config)
        : config(std::move(service_config)) {}

    SnapshotConfig config;
    ServiceState state = ServiceState::kCreated;
    bool media_bound = false;
    bool capturing = false;
    bool snap_vpss_started = false;
    bool vi_bound_snap_vpss = false;
    bool jpeg_venc_started = false;
    bool snap_vpss_bound_venc = false;
    bool snap_pipe_enabled = false;
    MediaChannels media_channels{};

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
};

SnapshotService::SnapshotService() : SnapshotService(SnapshotConfig{}) {}

SnapshotService::SnapshotService(const SnapshotConfig& config)
    : impl_(new Impl(config)) {}

SnapshotService::~SnapshotService() {
    Deinit();
    delete impl_;
    impl_ = nullptr;
}

infra::Status SnapshotService::Init() {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    if (!IsValidConfig(impl_->config)) {
        return infra::Status::kInvalidParam;
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
    return infra::Status::kOk;
}

void SnapshotService::Stop() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->CleanupCaptureSession();
    if (impl_->state == ServiceState::kStarted) {
        impl_->state = ServiceState::kStopped;
    }
}

void SnapshotService::Deinit() {
    if (impl_ == nullptr) {
        return;
    }
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
    if (impl_->state != ServiceState::kStarted) {
        return infra::Result<SnapshotFrame>::Fail(infra::Status::kBusy);
    }
    if (impl_->capturing) {
        return infra::Result<SnapshotFrame>::Fail(infra::Status::kBusy);
    }
    if (!IsValidRequest(request)) {
        return infra::Result<SnapshotFrame>::Fail(infra::Status::kInvalidParam);
    }

    impl_->capturing = true;
    const infra::Status prepare_error = impl_->PrepareCaptureSession();
    if (prepare_error != infra::Status::kOk) {
        impl_->CleanupCaptureSession();
        return infra::Result<SnapshotFrame>::Fail(prepare_error);
    }

    infra::Result<hisisdk::JpegFrame> hisi_frame =
        hisisdk::DefaultSdk().CaptureJpeg(
            ToHisiSnapshotConfig(impl_->config, request));

    impl_->CleanupCaptureSession();
    if (!hisi_frame.IsOk()) {
        return infra::Result<SnapshotFrame>::Fail(hisi_frame.status);
    }
    return infra::Result<SnapshotFrame>::Ok(ToSnapshotFrame(hisi_frame.value));
}

bool SnapshotService::IsCapturing() const {
    if (impl_ == nullptr) {
        return false;
    }
    return impl_->capturing;
}

}  // namespace live_stream
