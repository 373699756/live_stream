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
    : config_(std::move(config)) {}

infra::Result<MediaCapabilities> MediaPipeline::GetCapabilities() const {
    return hisisdk::DefaultSdk().GetCapabilities();
}

infra::Status MediaPipeline::InitSystem() {
    if (!IsValidMediaPipelineConfig(config_)) {
        return infra::Status::kInvalidParam;
    }

    const infra::Status status = hisisdk::DefaultSdk().InitSystem(config_);
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
        hisisdk::DefaultSdk().DeinitSystem();
    }
    system_initialized_ = false;
}

infra::Status MediaPipeline::Start() {
    if (!system_initialized_) {
        return infra::Status::kBusy;
    }

    infra::Status status = hisisdk::DefaultSdk().StartVi(config_);
    if (status != infra::Status::kOk) {
        return status;
    }
    vi_started_ = true;

    status = hisisdk::DefaultSdk().StartVpss(config_);
    if (status != infra::Status::kOk) {
        return status;
    }
    vpss_started_ = true;

    status = hisisdk::DefaultSdk().BindViVpss(config_);
    if (status != infra::Status::kOk) {
        return status;
    }
    vi_bound_vpss_ = true;

    status = hisisdk::DefaultSdk().StartVenc(config_);
    if (status != infra::Status::kOk) {
        return status;
    }
    venc_started_ = true;

    status = hisisdk::DefaultSdk().BindVpssVenc(config_);
    if (status != infra::Status::kOk) {
        return status;
    }
    vpss_bound_venc_ = true;

    status = hisisdk::DefaultSdk().StartVencStream(config_);
    if (status != infra::Status::kOk) {
        return status;
    }
    stream_started_ = true;
    return infra::Status::kOk;
}

void MediaPipeline::Stop() {
    if (stream_started_) {
        hisisdk::DefaultSdk().StopVencStream(config_);
        stream_started_ = false;
    }
    if (vpss_bound_venc_) {
        hisisdk::DefaultSdk().UnbindVpssVenc(config_);
        vpss_bound_venc_ = false;
    }
    if (venc_started_) {
        hisisdk::DefaultSdk().StopVenc(config_);
        venc_started_ = false;
    }
    if (vi_bound_vpss_) {
        hisisdk::DefaultSdk().UnbindViVpss(config_);
        vi_bound_vpss_ = false;
    }
    if (vpss_started_) {
        hisisdk::DefaultSdk().StopVpss(config_);
        vpss_started_ = false;
    }
    if (vi_started_) {
        hisisdk::DefaultSdk().StopVi(config_);
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
