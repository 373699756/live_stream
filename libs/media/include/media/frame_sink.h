#ifndef LIVE_STREAM_MEDIA_FRAME_SINK_H_
#define LIVE_STREAM_MEDIA_FRAME_SINK_H_

#include "media/media_frame.h"

namespace live_stream {

enum class MediaStreamState {
    kClosed = 0,
    kOpening,
    kRunning,
    kError,
};

class FrameSink {
public:
    virtual ~FrameSink() = default;

    // PushFrame 是同步回调。sink 如果要把 frame 放入异步队列，按值保存
    // MediaFrame 即可保活底层 MediaBuffer。
    virtual bool PushFrame(const MediaFrame &frame) = 0;
};

// platform/device adapter -> media 的同步帧回调。接收方按值保存 MediaFrame
// 即可延长 payload 生命周期。
using MediaFrameCallback = void (*)(const MediaFrame &frame, void *user);

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_FRAME_SINK_H_
