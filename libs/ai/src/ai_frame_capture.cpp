#include "ai_frame_capture.h"

#include "device.h"

namespace live_stream {
namespace ai_internal {
namespace {

MppChannel VpssChannelForStream(const MediaChannels &channels,
                                StreamId stream_id) {
    return stream_id == StreamId::kSub ? channels.sub_vpss : channels.vpss;
}

hisisdk::Size YuvSizeForStream(const MediaChannels &channels,
                               StreamId stream_id) {
    const VideoSize size = stream_id == StreamId::kSub ? channels.sub_size
                                                       : channels.main_size;
    return hisisdk::Size{size.width, size.height};
}

bool IsValidYuvCaptureTarget(const MediaChannels &channels,
                             StreamId stream_id) {
    const MppChannel vpss_channel =
        VpssChannelForStream(channels, stream_id);
    const hisisdk::Size size = YuvSizeForStream(channels, stream_id);
    return vpss_channel.module == MppModule::kVpss &&
           vpss_channel.device >= 0 && vpss_channel.channel >= 0 &&
           size.width > 0 && size.height > 0;
}

}  // namespace

AiFrameCapture::AiFrameCapture(hisisdk::IHisiSnapshot *snapshot,
                               DeviceMedia *device)
    : snapshot_(snapshot), device_(device) {}

bool AiFrameCapture::Available() const { return snapshot_ != nullptr; }

hisisdk::YuvFrame AiFrameCapture::Capture(const AiModelConfig &config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_ == nullptr || device_ == nullptr ||
        !device_->IsStarted() || device_->IsRestarting()) {
        return hisisdk::YuvFrame{};
    }
    const MediaChannels media_channels = device_->GetChannels();
    if (!IsValidYuvCaptureTarget(media_channels, config.stream_id)) {
        return hisisdk::YuvFrame{};
    }
    return snapshot_->CaptureYuvFrame(
        VpssChannelForStream(media_channels, config.stream_id),
        YuvSizeForStream(media_channels, config.stream_id),
        config.inference_interval_ms);
}

}  // namespace ai_internal
}  // namespace live_stream
