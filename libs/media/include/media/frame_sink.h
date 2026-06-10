#ifndef LIVE_STREAM_MEDIA_FRAME_SINK_H_
#define LIVE_STREAM_MEDIA_FRAME_SINK_H_

#include "media/encoded_frame.h"
#include "media/stream_types.h"
#include "media_codec.h"

#include <cstdint>
#include <string>

namespace live_stream {

enum class MediaStreamState {
    kClosed = 0,
    kOpening,
    kRunning,
    kError,
};

struct FramePayload {
    // 帧分发时使用的热路径对象。encoded_frame 持有 FrameBuffer 引用；
    // h264_units/h265_units 只是指向 encoded_frame payload 的 NAL 视图，不能脱离
    // encoded_frame 生命周期单独保存。
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
    // NAL list 可以浅拷贝，因为它们指向同一个 retained_frame payload。
    // 这里不会重新解析或复制视频码流。
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
