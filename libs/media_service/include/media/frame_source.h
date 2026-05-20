#ifndef LIVE_STREAM_MEDIA_FRAME_SOURCE_H_
#define LIVE_STREAM_MEDIA_FRAME_SOURCE_H_

#include "media/encoded_frame.h"
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

enum class KeyFrameReason {
    kNewClient = 0,
    kPacketLoss,
    kRecovery,
};

using FrameSubscriptionId = uint64_t;

struct FrameSubscribeOptions {
    StreamId stream_id = StreamId::kMain;
    bool require_key_frame_first = true;
    std::string sink_name;
};

class IFrameSink {
public:
    virtual ~IFrameSink() = default;

    virtual const char* Name() const = 0;
    virtual void OnFrame(const EncodedFrame& frame) = 0;
    virtual void OnSourceStateChanged(StreamId stream_id,
                                      StreamState state) = 0;
};

class IFrameSource {
public:
    virtual ~IFrameSource() = default;

    virtual bool IsStreamStarted(StreamId stream_id) const = 0;
    virtual VideoCodec GetStreamCodec(StreamId stream_id) const = 0;
    virtual FrameSubscriptionId SubscribeFrames(
        const FrameSubscribeOptions& options, IFrameSink* sink) = 0;
    virtual bool UnsubscribeFrames(FrameSubscriptionId subscription_id) = 0;
    virtual bool RequestKeyFrame(StreamId stream_id,
                                 KeyFrameReason reason) = 0;
};

using EncodedFrameCallback = void (*)(const EncodedFrame& frame,
                                      void* user);

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_FRAME_SOURCE_H_
