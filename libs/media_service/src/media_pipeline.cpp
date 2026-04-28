#include "media_pipeline.h"

#include "hisisdk/hisi_sdk.h"

#include <utility>

namespace live_stream {
namespace {

bool IsValidSize(const VideoSize& size) {
    return size.width > 0 && size.height > 0;
}

bool IsValidFrameRate(const FrameRate& frame_rate) {
    return frame_rate.source_fps > 0 && frame_rate.target_fps > 0 &&
           frame_rate.target_fps <= frame_rate.source_fps;
}

}  // namespace

bool IsValidMediaPipelineConfig(const MediaPipelineConfig& config) {
    return config.sensor_id >= 0 && config.video_pipe >= 0 &&
           config.snap_pipe >= 0 && config.vi_channel >= 0 &&
           config.vpss_group >= 0 && config.vpss_channel >= 0 &&
           config.venc_channel >= 0 && config.vb_block_count > 0 &&
           IsValidSize(config.main_stream.size) &&
           IsValidFrameRate(config.main_stream.frame_rate) &&
           config.main_stream.bitrate_kbps > 0 && config.main_stream.gop > 0;
}

bool IsValidMediaStream(infra::StreamId stream_id) {
    return stream_id == infra::StreamId::kMain ||
           stream_id == infra::StreamId::kSub;
}

MediaPipeline::MediaPipeline(MediaPipelineConfig config)
    : MediaPipeline(std::move(config), &hisisdk::DefaultSdk()) {}

MediaPipeline::MediaPipeline(MediaPipelineConfig config, hisisdk::IHisiSdk* sdk)
    : sdk_(sdk != nullptr ? sdk : &hisisdk::DefaultSdk()),
      config_(std::move(config)) {}

void MediaPipeline::SetConfig(const MediaPipelineConfig& config) {
    config_ = config;
    BuildChannels();
}

void MediaPipeline::SetFrameCallback(EncodedFrameCallback callback, void* user) {
    frame_callback_ = callback;
    frame_callback_user_ = user;
}

infra::Result<MediaCapabilities> MediaPipeline::GetCapabilities() const {
    return sdk_->GetCapabilities();
}

infra::Status MediaPipeline::InitSystem() {
    if (!IsValidMediaPipelineConfig(config_)) {
        return infra::Status::kInvalidParam;
    }

    const infra::Status status = sdk_->InitSystem(config_);
    if (status != infra::Status::kOk) {
        return status;
    }
    BuildChannels();
    system_initialized_ = true;
    return infra::Status::kOk;
}

void MediaPipeline::DeinitSystem() {
    Stop();
    if (system_initialized_) {
        sdk_->DeinitSystem();
    }
    system_initialized_ = false;
}

infra::Status MediaPipeline::Start() {
    if (!system_initialized_) {
        return infra::Status::kBusy;
    }

    infra::Status status = sdk_->StartVi(config_);
    if (status != infra::Status::kOk) {
        return status;
    }
    vi_started_ = true;

    status = sdk_->StartVpss(config_);
    if (status != infra::Status::kOk) {
        Stop();
        return status;
    }
    vpss_started_ = true;

    status = sdk_->BindViVpss(config_);
    if (status != infra::Status::kOk) {
        Stop();
        return status;
    }
    vi_bound_vpss_ = true;

    status = sdk_->StartVenc(config_);
    if (status != infra::Status::kOk) {
        Stop();
        return status;
    }
    venc_started_ = true;

    status = sdk_->BindVpssVenc(config_);
    if (status != infra::Status::kOk) {
        Stop();
        return status;
    }
    vpss_bound_venc_ = true;

    status = sdk_->StartVencStream(config_, frame_callback_,
                                   frame_callback_user_);
    if (status != infra::Status::kOk) {
        Stop();
        return status;
    }
    stream_started_ = true;
    return infra::Status::kOk;
}

void MediaPipeline::Stop() {
    if (stream_started_) {
        sdk_->StopVencStream(config_);
        stream_started_ = false;
    }
    if (vpss_bound_venc_) {
        sdk_->UnbindVpssVenc(config_);
        vpss_bound_venc_ = false;
    }
    if (venc_started_) {
        sdk_->StopVenc(config_);
        venc_started_ = false;
    }
    if (vi_bound_vpss_) {
        sdk_->UnbindViVpss(config_);
        vi_bound_vpss_ = false;
    }
    if (vpss_started_) {
        sdk_->StopVpss(config_);
        vpss_started_ = false;
    }
    if (vi_started_) {
        sdk_->StopVi(config_);
        vi_started_ = false;
    }
}

void MediaPipeline::BuildChannels() {
    channels_.vi = MppChannel{MppModule::kVi, config_.video_pipe,
                              config_.vi_channel};
    channels_.vpss = MppChannel{MppModule::kVpss, config_.vpss_group,
                                config_.vpss_channel};
    channels_.venc = MppChannel{MppModule::kVenc, 0, config_.venc_channel};
    channels_.video_pipe = config_.video_pipe;
    channels_.snap_pipe = config_.snap_pipe;
}

}  // namespace live_stream
