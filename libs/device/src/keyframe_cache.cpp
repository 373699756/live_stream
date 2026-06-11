#include "keyframe_cache.h"

#include "media_channels.h"
#include "infra/log.h"
#include "media/media_buffer.h"
#include "media_codec.h"

#include <cstring>

namespace live_stream {
namespace device_internal {
namespace {

bool EncodedFrameHasCompleteParameterSets(const EncodedFrame &frame) {
    const uint8_t *data = EncodedFramePayloadData(&frame);
    const FrameSlice payload = EncodedFramePayloadSlice(&frame);
    if (data == nullptr ||
        (frame.codec != Codec::kH264 &&
         frame.codec != Codec::kH265)) {
        return false;
    }

    if (frame.codec == Codec::kH265) {
        media_codec::H265NalUnitList units;
        return media_codec::ParseH265AnnexBNalUnits(data, payload.size,
                                                    &units) &&
               media_codec::HasCompleteH265ParameterSets(units);
    }

    media_codec::H264NalUnitList units;
    return media_codec::ParseH264AnnexBNalUnits(data, payload.size, &units) &&
           media_codec::HasCompleteH264ParameterSets(units);
}

bool EncodedFrameHasKeyPicture(const EncodedFrame &frame) {
    if (frame.frame_type == FrameType::kIdr ||
        frame.frame_type == FrameType::kI ||
        frame.frame_type == FrameType::kJpeg) {
        return true;
    }
    const uint8_t *data = EncodedFramePayloadData(&frame);
    if (data == nullptr) {
        return false;
    }

    if (frame.codec == Codec::kH265) {
        media_codec::H265NalUnitList units;
        const FrameSlice payload = EncodedFramePayloadSlice(&frame);
        return media_codec::ParseH265AnnexBNalUnits(data, payload.size,
                                                    &units) &&
               media_codec::HasH265Keyframe(units);
    }
    if (frame.codec == Codec::kH264) {
        media_codec::H264NalUnitList units;
        const FrameSlice payload = EncodedFramePayloadSlice(&frame);
        return media_codec::ParseH264AnnexBNalUnits(data, payload.size,
                                                    &units) &&
               media_codec::HasH264Keyframe(units);
    }
    return false;
}

EncodedFrame CloneEncodedFramePayload(const EncodedFrame &frame) {
    EncodedFrame copy;
    copy.stream_id = frame.stream_id;
    copy.codec = frame.codec;
    copy.frame_type = frame.frame_type;
    copy.sequence = frame.sequence;
    copy.pts_us = frame.pts_us;
    copy.dts_us = frame.dts_us;
    const uint8_t *payload = EncodedFramePayloadData(&frame);
    const FrameSlice payload_slice = EncodedFramePayloadSlice(&frame);
    if (payload == nullptr || payload_slice.size == 0) {
        return copy;
    }
    // device 的最近关键帧缓存是有意的深拷贝：它要独立于实时帧的
    // FrameBuffer 引用生命周期存在，供新订阅方 keyframe-first 立即起播。
    FrameBuffer *buffer = FrameBufferAlloc(payload_slice.size);
    if (buffer == nullptr) {
        Error("device",
              "clone keyframe alloc failed stream=%s seq=%llu size=%u",
              StreamName(frame.stream_id),
              static_cast<unsigned long long>(frame.sequence),
              payload_slice.size);
        return EncodedFrame{};
    }
    std::memcpy(buffer->data, payload, payload_slice.size);
    if (!FrameBufferSetSize(buffer, payload_slice.size)) {
        FrameBufferUnref(buffer);
        return EncodedFrame{};
    }
    copy.payload.buffer = buffer;
    copy.payload.offset = 0;
    copy.payload.size = payload_slice.size;
    return copy;
}

}  // namespace

KeyframeCache::~KeyframeCache() { Clear(); }

void KeyframeCache::Remember(const EncodedFrame &frame) {
    if (!EncodedFrameHasKeyPicture(frame)) {
        return;
    }
    const bool has_parameter_sets =
        EncodedFrameHasCompleteParameterSets(frame);
    CachedKeyframe *cached = FindMutable(frame.stream_id);
    if (cached == nullptr) {
        return;
    }
    if (cached->has_frame && cached->has_parameter_sets &&
        !has_parameter_sets) {
        // 已有带完整参数集的关键帧时，不用不完整的新关键帧覆盖，避免新订阅方
        // 拿不到 SPS/PPS/VPS。
        return;
    }
    EncodedFrame cached_frame = CloneEncodedFramePayload(frame);
    if (!EncodedFrameHasPayload(&cached_frame)) {
        return;
    }
    (void)EncodedFrameMove(&cached->frame, &cached_frame);
    cached->has_frame = true;
    cached->has_parameter_sets = has_parameter_sets;
}

void KeyframeCache::Clear() {
    EncodedFrameUnref(&main_.frame);
    EncodedFrameUnref(&sub_.frame);
    main_ = CachedKeyframe{};
    sub_ = CachedKeyframe{};
}

bool KeyframeCache::Get(StreamId stream_id, EncodedFrame *frame) const {
    if (frame == nullptr) {
        return false;
    }
    const CachedKeyframe *cached = Find(stream_id);
    if (cached == nullptr || !cached->has_frame) {
        return false;
    }
    return EncodedFrameRefCopy(frame, &cached->frame);
}

KeyframeCache::CachedKeyframe *KeyframeCache::FindMutable(
    StreamId stream_id) {
    if (stream_id == StreamId::kMain) {
        return &main_;
    }
    if (stream_id == StreamId::kSub) {
        return &sub_;
    }
    return nullptr;
}

const KeyframeCache::CachedKeyframe *KeyframeCache::Find(
    StreamId stream_id) const {
    if (stream_id == StreamId::kMain) {
        return &main_;
    }
    if (stream_id == StreamId::kSub) {
        return &sub_;
    }
    return nullptr;
}

}  // namespace device_internal
}  // namespace live_stream
