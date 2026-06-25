#include "media_pipeline.h"

#include "hisi_vendor/sdk.h"
#include "infra/log.h"
#include "media_channels.h"

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

bool IsValidStreamConfig(const VideoStreamConfig& stream) {
    return IsValidSize(stream.size) && IsValidFrameRate(stream.frame_rate) &&
           stream.bitrate_kbps > 0 && stream.gop > 0;
}

bool IsValidSubStreamChannels(const MediaPipelineConfig& config) {
    if (!config.sub_stream.enabled) {
        return true;
    }
    return config.sub_vpss_channel >= 0 && config.sub_venc_channel >= 0 &&
           config.sub_vpss_channel != config.vpss_channel &&
           config.sub_venc_channel != config.venc_channel;
}

bool IsValidSnapshotChannels(const MediaPipelineConfig& config) {
    const hisisdk::SnapshotConfig snapshot;
    if (snapshot.jpeg_venc_channel == config.venc_channel) {
        return false;
    }
    return !config.sub_stream.enabled ||
           snapshot.jpeg_venc_channel != config.sub_venc_channel;
}

}  // namespace

bool IsValidMediaPipelineConfig(const MediaPipelineConfig& config) {
    return config.sensor_id >= 0 && config.video_pipe >= 0 &&
           config.snap_pipe >= 0 && config.vi_channel >= 0 &&
           config.vpss_group >= 0 && config.vpss_channel >= 0 &&
           config.venc_channel >= 0 && IsValidSubStreamChannels(config) &&
           IsValidSnapshotChannels(config) &&
           config.vb_blocks > 0 && IsValidStreamConfig(config.main_stream) &&
           config.main_stream.stream_id == StreamId::kMain &&
           IsValidStreamConfig(config.sub_stream) &&
           config.sub_stream.stream_id == StreamId::kSub;
}

bool IsValidMediaStream(StreamId stream_id) {
    return stream_id == StreamId::kMain ||
           stream_id == StreamId::kSub;
}

MediaPipeline::MediaPipeline(MediaPipelineConfig config,
                             hisisdk::HisiSdk sdk)
    : system_(sdk.system),
      media_pipeline_(sdk.media_pipeline),
      venc_stream_(sdk.venc_stream),
      image_(sdk.image),
      config_(std::move(config)) {}

void MediaPipeline::SetConfig(const MediaPipelineConfig& config) {
    config_ = config;
    BuildChannels();
}

void MediaPipeline::SetFrameCallback(MediaFrameCallback callback,
                                     void* user) {
    frame_callback_ = callback;
    frame_callback_user_ = user;
}

MediaCapabilities MediaPipeline::GetCapabilities() const {
    return system_->GetCapabilities();
}

bool MediaPipeline::InitSystem() {
    if (!IsValidMediaPipelineConfig(config_)) {
        return false;
    }

    Info("device",
         "MPP system init begin sensor=%d pipe=%d vi=%d vpss=%d:%d "
         "venc=%d main=%ux%u sub_enabled=%d sub=%ux%u",
         config_.sensor_id, config_.video_pipe, config_.vi_channel,
         config_.vpss_group, config_.vpss_channel, config_.venc_channel,
         config_.main_stream.size.width, config_.main_stream.size.height,
         config_.sub_stream.enabled ? 1 : 0,
         config_.sub_stream.size.width, config_.sub_stream.size.height);
    if (!system_->InitSystem(config_)) {
        return false;
    }
    BuildChannels();
    system_initialized_ = true;
    Info("device", "MPP system init done");
    return true;
}

bool MediaPipeline::DeinitSystem() {
    Stop();
    if (!system_->DeinitSystem()) {
        return false;
    }
    system_initialized_ = false;
    return true;
}

bool MediaPipeline::Start() {
    if (!system_initialized_) {
        return false;
    }

    Info("device", "Start VI begin");
    if (!media_pipeline_->StartVi(config_)) {
        return false;
    }
    vi_started_ = true;
    Info("device", "Start VI done");

    Info("device", "Start VPSS begin");
    if (!media_pipeline_->StartVpss(config_)) {
        Stop();
        return false;
    }
    vpss_started_ = true;
    Info("device", "Start VPSS done");

    Info("device", "Bind VI-VPSS begin");
    if (!media_pipeline_->BindViVpss(config_)) {
        Stop();
        return false;
    }
    vi_bound_vpss_ = true;
    Info("device", "Bind VI-VPSS done");

    // VENC receive is started by BindVpssVenc() after the source is connected.
    Info("device", "Start VENC begin");
    if (!media_pipeline_->StartVenc(config_)) {
        Stop();
        return false;
    }
    venc_started_ = true;
    Info("device", "Start VENC done");

    Info("device", "Bind VPSS-VENC begin");
    if (!media_pipeline_->BindVpssVenc(config_)) {
        Stop();
        return false;
    }
    vpss_bound_venc_ = true;
    Info("device", "Bind VPSS-VENC done");

    Info("device", "Start VENC stream begin");
    if (!venc_stream_->StartVencStream(config_, frame_callback_,
                                       frame_callback_user_)) {
        Stop();
        return false;
    }
    stream_started_ = true;
    Info("device", "Start VENC stream done");
    return true;
}

void MediaPipeline::Stop() {
    if (stream_started_) {
        venc_stream_->StopVencStream(config_);
        stream_started_ = false;
    }
    if (vpss_bound_venc_) {
        media_pipeline_->UnbindVpssVenc(config_);
        vpss_bound_venc_ = false;
    }
    if (venc_started_) {
        media_pipeline_->StopVenc(config_);
        venc_started_ = false;
    }
    if (vi_bound_vpss_) {
        media_pipeline_->UnbindViVpss(config_);
        vi_bound_vpss_ = false;
    }
    if (vpss_started_) {
        media_pipeline_->StopVpss(config_);
        vpss_started_ = false;
    }
    if (vi_started_) {
        media_pipeline_->StopVi(config_);
        vi_started_ = false;
    }
}

bool MediaPipeline::ApplyVencRoi(StreamId stream_id) {
    const VideoStreamConfig* stream =
        device_internal::FindConfiguredStream(config_, stream_id);
    if (stream == nullptr || !stream->enabled) {
        return false;
    }
    const int32_t venc_channel =
        device_internal::VencChannelForStream(config_, stream_id);
    return venc_stream_->ApplyVencRoi(venc_channel, *stream);
}

bool MediaPipeline::ApplyImageConfig(const Json& image_config) {
    return image_->ApplyImageConfig(config_, image_config);
}

hisisdk::ExposureInfo MediaPipeline::QueryExposureInfo() const {
    return image_->QueryExposureInfo(config_);
}

void MediaPipeline::BuildChannels() {
    channels_.vi = MppChannel{MppModule::kVi, config_.video_pipe,
                              config_.vi_channel};
    channels_.vpss = MppChannel{MppModule::kVpss, config_.vpss_group,
                                config_.vpss_channel};
    channels_.sub_vpss = MppChannel{MppModule::kVpss, config_.vpss_group,
                                    config_.sub_vpss_channel};
    channels_.venc = MppChannel{MppModule::kVenc, 0, config_.venc_channel};
    channels_.sub_venc = MppChannel{MppModule::kVenc, 0,
                                    config_.sub_venc_channel};
    channels_.video_pipe = config_.video_pipe;
    channels_.snap_pipe = config_.snap_pipe;
    channels_.main_size = config_.main_stream.size;
    channels_.sub_size = config_.sub_stream.size;
}

}  // namespace live_stream
