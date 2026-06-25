#include "ai_frame_capture.h"

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

}  // namespace

AiFrameCapture::AiFrameCapture(hisisdk::IHisiSnapshot *snapshot,
                               const MediaChannels &media_channels)
    : snapshot_(snapshot), media_channels_(media_channels) {}

bool AiFrameCapture::Available() const { return snapshot_ != nullptr; }

hisisdk::YuvFrame AiFrameCapture::Capture(const AiModelConfig &config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_ == nullptr) {
        return hisisdk::YuvFrame{};
    }
    return snapshot_->CaptureYuvFrame(
        VpssChannelForStream(media_channels_, config.stream_id),
        YuvSizeForStream(media_channels_, config.stream_id),
        config.inference_interval_ms);
}

}  // namespace ai_internal
}  // namespace live_stream
