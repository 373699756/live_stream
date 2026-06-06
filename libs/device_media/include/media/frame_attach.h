#ifndef LIVE_STREAM_MEDIA_FRAME_ATTACH_H_
#define LIVE_STREAM_MEDIA_FRAME_ATTACH_H_

#include "media/encoded_frame.h"
#include "media/stream_types.h"
#include "media_codec.h"

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

struct FramePayload {
    EncodedFrame encoded_frame;
    bool has_nal_units = false;
    media_codec::H264NalUnitList h264_units;
    media_codec::H265NalUnitList h265_units;
};

inline void FramePayloadInit(FramePayload *payload) {
    if (payload == nullptr) {
        return;
    }
    EncodedFrameInit(&payload->encoded_frame);
    payload->has_nal_units = false;
    payload->h264_units = media_codec::H264NalUnitList{};
    payload->h265_units = media_codec::H265NalUnitList{};
}

inline void FramePayloadUnref(FramePayload *payload) {
    if (payload == nullptr) {
        return;
    }
    EncodedFrameUnref(&payload->encoded_frame);
    payload->has_nal_units = false;
    payload->h264_units = media_codec::H264NalUnitList{};
    payload->h265_units = media_codec::H265NalUnitList{};
}

inline bool FramePayloadRefCopy(FramePayload *target,
                                const FramePayload *source) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    if (target == source) {
        return true;
    }
    EncodedFrame retained_frame;
    if (!EncodedFrameRefCopy(&retained_frame, &source->encoded_frame)) {
        return false;
    }
    FramePayloadUnref(target);
    target->encoded_frame = retained_frame;
    target->has_nal_units = source->has_nal_units;
    target->h264_units = source->h264_units;
    target->h265_units = source->h265_units;
    return true;
}

inline bool FramePayloadMove(FramePayload *target, FramePayload *source) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    if (target == source) {
        return true;
    }
    FramePayloadUnref(target);
    target->has_nal_units = source->has_nal_units;
    target->h264_units = source->h264_units;
    target->h265_units = source->h265_units;
    source->has_nal_units = false;
    source->h264_units = media_codec::H264NalUnitList{};
    source->h265_units = media_codec::H265NalUnitList{};
    return EncodedFrameMove(&target->encoded_frame, &source->encoded_frame);
}

struct FrameAttachOptions {
    StreamId stream_id = StreamId::kMain;
    bool require_key_frame_first = true;
    std::string sink_name;
};

class IFrameSink {
public:
    virtual ~IFrameSink() = default;

    virtual const char* Name() const = 0;
    virtual void OnFrame(const FramePayload& frame) = 0;
    virtual void OnSourceStateChanged(StreamId stream_id,
                                      StreamState state) = 0;
};

using EncodedFrameCallback = void (*)(const EncodedFrame& frame, void* user);

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_FRAME_ATTACH_H_
