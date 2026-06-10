#ifndef LIVE_STREAM_MEDIA_FRAME_SINK_H_
#define LIVE_STREAM_MEDIA_FRAME_SINK_H_

#include "media/encoded_frame.h"

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

    // PushFrame 是同步回调。sink 如果要把 frame 放入异步队列，必须在回调内
    // EncodedFrameRefCopy()；回调返回后上游会释放自己的引用。
    virtual bool PushFrame(const EncodedFrame &frame) = 0;
};

// platform/device adapter -> media 的同步帧回调。frame 内存由 callback 调用方临时持有；
// 接收方若要保存必须 EncodedFrameRefCopy()。
using EncodedFrameCallback = void (*)(const EncodedFrame &frame, void *user);

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_FRAME_SINK_H_
