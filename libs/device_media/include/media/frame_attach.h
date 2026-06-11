#ifndef LIVE_STREAM_MEDIA_FRAME_ATTACH_H_
#define LIVE_STREAM_MEDIA_FRAME_ATTACH_H_

#include "media/frame_sink.h"
#include "media/stream_types.h"

#include <cstdint>
#include <string>

namespace live_stream {

enum class StreamState {
    kClosed = 0,
    kOpening,
    kRunning,
    kError,
};

using FrameAttachId = uint64_t;

struct FrameAttachOptions {
    StreamId stream_id = StreamId::kMain;
    bool require_key_frame_first = true;
    std::string sink_name;
};

class IFrameSink : public FrameSink {
public:
    ~IFrameSink() override = default;

    virtual const char* Name() const = 0;
    // PushFrame 是同步回调。sink 如果要把 frame 放入异步队列，必须在回调内
    // EncodedFrameRefCopy()；回调返回后上游会释放自己的引用。
    virtual void OnSourceStateChanged(StreamId stream_id,
                                      StreamState state) = 0;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_FRAME_ATTACH_H_
