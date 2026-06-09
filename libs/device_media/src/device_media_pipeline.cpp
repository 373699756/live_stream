#include "device_media_pipeline.h"

#include "hisisdk/hisi_sdk.h"
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
           config.vb_block_count > 0 && IsValidStreamConfig(config.main_stream) &&
           config.main_stream.stream_id == StreamId::kMain &&
           IsValidStreamConfig(config.sub_stream) &&
           config.sub_stream.stream_id == StreamId::kSub;
}

bool IsValidMediaStream(StreamId stream_id) {
    return stream_id == StreamId::kMain ||
           stream_id == StreamId::kSub;
}

DeviceMediaPipeline::DeviceMediaPipeline(MediaPipelineConfig config)
    : DeviceMediaPipeline(std::move(config), &hisisdk::DefaultSdk()) {}

DeviceMediaPipeline::DeviceMediaPipeline(MediaPipelineConfig config,
                                         hisisdk::IHisiSdk* sdk)
    : sdk_(sdk != nullptr ? sdk : &hisisdk::DefaultSdk()),
      config_(std::move(config)) {}

void DeviceMediaPipeline::SetConfig(const MediaPipelineConfig& config) {
    config_ = config;
    BuildChannels();
}

void DeviceMediaPipeline::SetFrameCallback(EncodedFrameCallback callback,
                                           void* user) {
    frame_callback_ = callback;
    frame_callback_user_ = user;
}

MediaCapabilities DeviceMediaPipeline::GetCapabilities() const {
    return sdk_->GetCapabilities();
}

bool DeviceMediaPipeline::InitSystem() {
    if (!IsValidMediaPipelineConfig(config_)) {
        return false;
    }

    if (!sdk_->InitSystem(config_)) {
        return false;
    }
    BuildChannels();
    system_initialized_ = true;
    return true;
}

bool DeviceMediaPipeline::DeinitSystem() {
    Stop();
    if (!sdk_->DeinitSystem()) {
        return false;
    }
    system_initialized_ = false;
    return true;
}

bool DeviceMediaPipeline::Start() {
    if (!system_initialized_) {
        return false;
    }

    if (!sdk_->StartVi(config_)) {
        return false;
    }
    vi_started_ = true;

    if (!sdk_->StartVpss(config_)) {
        Stop();
        return false;
    }
    vpss_started_ = true;

    if (!sdk_->BindViVpss(config_)) {
        Stop();
        return false;
    }
    vi_bound_vpss_ = true;

    // VENC receive is started by BindVpssVenc() after the source is connected.
    if (!sdk_->StartVenc(config_)) {
        Stop();
        return false;
    }
    venc_started_ = true;

    if (!sdk_->BindVpssVenc(config_)) {
        Stop();
        return false;
    }
    vpss_bound_venc_ = true;

    if (!sdk_->StartVencStream(config_, frame_callback_,
                               frame_callback_user_)) {
        Stop();
        return false;
    }
    stream_started_ = true;
    return true;
}

void DeviceMediaPipeline::Stop() {
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

bool DeviceMediaPipeline::ApplyVencRoi(StreamId stream_id) {
    const VideoStreamConfig* stream =
        device_media_internal::FindConfiguredStream(config_, stream_id);
    if (stream == nullptr || !stream->enabled) {
        return false;
    }
    const int32_t venc_channel =
        device_media_internal::VencChannelForStream(config_, stream_id);
    return sdk_->ApplyVencRoi(venc_channel, *stream);
}

bool DeviceMediaPipeline::ApplyImageConfig(const ConfigJson& image_config) {
    return sdk_->ApplyImageConfig(config_, image_config);
}

hisisdk::ExposureInfo DeviceMediaPipeline::QueryExposureInfo() const {
    return sdk_->QueryExposureInfo(config_);
}

void DeviceMediaPipeline::BuildChannels() {
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
