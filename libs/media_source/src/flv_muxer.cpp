#include "flv_muxer.h"

namespace live_stream {
namespace media_source_internal {

std::string FlvMuxer::BuildFileHeader() {
    return media_mux::BuildFlvFileHeader();
}

std::string FlvMuxer::BuildSequenceHeader(VideoCodec codec,
                                          const std::string &vps,
                                          const std::string &sps,
                                          const std::string &pps,
                                          uint32_t timestamp_ms) {
    if (codec == VideoCodec::kH264) {
        if (sps.empty() || pps.empty()) {
            return std::string();
        }
        return media_mux::BuildH264FlvSequenceHeaderTag(sps, pps,
                                                         timestamp_ms);
    }
    if (codec == VideoCodec::kH265) {
        if (vps.empty() || sps.empty() || pps.empty()) {
            return std::string();
        }
        return media_mux::BuildH265FlvSequenceHeaderTag(vps, sps, pps,
                                                         timestamp_ms);
    }
    return std::string();
}

bool FlvMuxer::BuildVideoTagView(const EncodedFrame &frame,
                                 const FramePayload &payload,
                                 bool keyframe,
                                 FlvVideoTagView *tag_view) {
    if (tag_view == nullptr || frame.codec != payload.encoded_frame.codec) {
        return false;
    }
    const int64_t composition_time_ms = (frame.pts_us - frame.dts_us) / 1000;
    const uint32_t timestamp_ms = static_cast<uint32_t>(frame.dts_us / 1000);
    if (frame.codec == VideoCodec::kH264) {
        return media_mux::BuildH264FlvVideoTagView(
            keyframe, static_cast<int32_t>(composition_time_ms), timestamp_ms,
            payload.h264_units, tag_view);
    }
    if (frame.codec == VideoCodec::kH265) {
        return media_mux::BuildH265FlvVideoTagView(
            keyframe, static_cast<int32_t>(composition_time_ms), timestamp_ms,
            payload.h265_units, tag_view);
    }
    return false;
}

}  // namespace media_source_internal
}  // namespace live_stream
