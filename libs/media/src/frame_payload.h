#ifndef LIVE_STREAM_MEDIA_SRC_FRAME_PAYLOAD_H_
#define LIVE_STREAM_MEDIA_SRC_FRAME_PAYLOAD_H_

#include "media/media_frame.h"
#include "media_codec.h"

namespace live_stream {
namespace media_internal {

struct FramePayload {
    // 帧分发时使用的热路径对象。frame 持有 MediaBuffer 引用；
    // h264_units/h265_units 只是指向 frame payload 的 NAL 视图，不能脱离
    // frame 生命周期单独保存。
    MediaFrame frame;
    bool has_nal_units = false;
    media_codec::H264NalUnitList h264_units;
    media_codec::H265NalUnitList h265_units;
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_FRAME_PAYLOAD_H_
