#include "dm_key.h"

#include "dm_chan.h"
#include "infra/log.h"
#include "media/media_buffer.h"
#include "media_codec.h"

#include <cstring>

namespace live_stream {
namespace device_media_internal {
namespace {

bool EncodedFrameHasCompleteParameterSets(const EncodedFrame &frame) {
    const uint8_t *data = EncodedFramePayloadData(&frame);
    if (data == nullptr ||
        (frame.codec != VideoCodec::kH264 &&
         frame.codec != VideoCodec::kH265)) {
        return false;
    }

    if (frame.codec == VideoCodec::kH265) {
        media_codec::H265NalUnitList units;
        return media_codec::ParseH265AnnexBNalUnits(data, frame.size,
                                                    &units) &&
               media_codec::HasCompleteH265ParameterSets(units);
    }

    media_codec::H264NalUnitList units;
    return media_codec::ParseH264AnnexBNalUnits(data, frame.size, &units) &&
           media_codec::HasCompleteH264ParameterSets(units);
}

bool EncodedFrameHasKeyPicture(const EncodedFrame &frame) {
    if (media_codec::IsKeyFrame(frame.frame_type) ||
        frame.frame_type == FrameType::kJpeg) {
        return true;
    }
    const uint8_t *data = EncodedFramePayloadData(&frame);
    if (data == nullptr) {
        return false;
    }

    if (frame.codec == VideoCodec::kH265) {
        media_codec::H265NalUnitList units;
        return media_codec::ParseH265AnnexBNalUnits(data, frame.size,
                                                    &units) &&
               media_codec::HasH265KeyFrame(units);
    }
    if (frame.codec == VideoCodec::kH264) {
        media_codec::H264NalUnitList units;
        return media_codec::ParseH264AnnexBNalUnits(data, frame.size,
                                                    &units) &&
               media_codec::HasH264KeyFrame(units);
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
    if (payload == nullptr || frame.size == 0) {
        return copy;
    }
    VideoBuffer *buffer = VideoBufferAlloc(frame.size);
    if (buffer == nullptr) {
        Error("device_media",
              "clone key frame alloc failed stream=%s seq=%llu size=%u",
              StreamName(frame.stream_id),
              static_cast<unsigned long long>(frame.sequence), frame.size);
        return EncodedFrame{};
    }
    std::memcpy(buffer->data, payload, frame.size);
    if (!VideoBufferSetSize(buffer, frame.size)) {
        VideoBufferUnref(buffer);
        return EncodedFrame{};
    }
    copy.buffer = buffer;
    copy.offset = 0;
    copy.size = frame.size;
    return copy;
}

}  // namespace

KeyFrameCache::~KeyFrameCache() { Clear(); }

void KeyFrameCache::Remember(const EncodedFrame &frame) {
    if (!EncodedFrameHasKeyPicture(frame)) {
        return;
    }
    const bool has_parameter_sets =
        EncodedFrameHasCompleteParameterSets(frame);
    CachedKeyFrame *cached = FindMutable(frame.stream_id);
    if (cached == nullptr) {
        return;
    }
    if (cached->has_frame && cached->has_parameter_sets &&
        !has_parameter_sets) {
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

void KeyFrameCache::Clear() {
    EncodedFrameUnref(&main_.frame);
    EncodedFrameUnref(&sub_.frame);
    main_ = CachedKeyFrame{};
    sub_ = CachedKeyFrame{};
}

bool KeyFrameCache::Get(StreamId stream_id, EncodedFrame *frame) const {
    if (frame == nullptr) {
        return false;
    }
    const CachedKeyFrame *cached = Find(stream_id);
    if (cached == nullptr || !cached->has_frame) {
        return false;
    }
    return EncodedFrameRefCopy(frame, &cached->frame);
}

KeyFrameCache::CachedKeyFrame *KeyFrameCache::FindMutable(
    StreamId stream_id) {
    if (stream_id == StreamId::kMain) {
        return &main_;
    }
    if (stream_id == StreamId::kSub) {
        return &sub_;
    }
    return nullptr;
}

const KeyFrameCache::CachedKeyFrame *KeyFrameCache::Find(
    StreamId stream_id) const {
    if (stream_id == StreamId::kMain) {
        return &main_;
    }
    if (stream_id == StreamId::kSub) {
        return &sub_;
    }
    return nullptr;
}

}  // namespace device_media_internal
}  // namespace live_stream
