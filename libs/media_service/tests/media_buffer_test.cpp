#include "media/encoded_frame.h"

#include <cstdint>
#include <cstring>
#include <memory>

using live_stream::CreateFixedMediaBufferPool;
using live_stream::CreateMediaBuffer;
using live_stream::EncodedFrame;
using live_stream::FrameType;
using live_stream::IMediaBuffer;
using live_stream::IMediaBufferPool;
using live_stream::IsValidBufferSlice;
using live_stream::MediaBufferPoolStats;
using live_stream::StreamId;
using live_stream::VideoCodec;

int main() {
    std::shared_ptr<IMediaBuffer> buffer = CreateMediaBuffer(16);
    if (!buffer || buffer->Capacity() != 16U || buffer->Size() != 0U) {
        return 1;
    }

    const char* payload = "frame";
    std::memcpy(buffer->MutableData(), payload, std::strlen(payload));
    if (!buffer->SetSize(static_cast<uint32_t>(std::strlen(payload)))) {
        return 2;
    }
    if (buffer->Size() != 5U ||
        std::memcmp(buffer->Data(), payload, std::strlen(payload)) != 0) {
        return 3;
    }
    if (buffer->SetSize(buffer->Capacity() + 1U) || buffer->Size() != 5U) {
        return 4;
    }

    EncodedFrame frame;
    frame.stream_id = StreamId::kMain;
    frame.codec = VideoCodec::kH265;
    frame.frame_type = FrameType::kIdr;
    frame.sequence = 9;
    frame.pts_us = 100;
    frame.dts_us = 90;
    frame.buffer = buffer;
    frame.offset = 0;
    frame.size = buffer->Size();

    if (frame.buffer.use_count() != 2 || frame.size != 5U ||
        frame.codec != VideoCodec::kH265) {
        return 5;
    }
    if (!IsValidBufferSlice(frame.PayloadSlice())) {
        return 6;
    }

    std::shared_ptr<IMediaBufferPool> pool = CreateFixedMediaBufferPool(8, 2);
    if (!pool) {
        return 7;
    }
    std::shared_ptr<IMediaBuffer> pooled_a = pool->Acquire();
    std::shared_ptr<IMediaBuffer> pooled_b = pool->Acquire();
    std::shared_ptr<IMediaBuffer> pooled_c = pool->Acquire();
    if (!pooled_a || !pooled_b || pooled_c) {
        return 8;
    }
    MediaBufferPoolStats stats = pool->Stats();
    if (stats.in_use_count != 2U || stats.free_count != 0U ||
        stats.no_memory_count != 1U) {
        return 9;
    }
    pooled_a.reset();
    stats = pool->Stats();
    if (stats.in_use_count != 1U || stats.free_count != 1U) {
        return 10;
    }
    pooled_b.reset();
    stats = pool->Stats();
    if (stats.in_use_count != 0U || stats.free_count != 2U ||
        stats.high_water_count != 2U) {
        return 11;
    }

    pooled_a = pool->Acquire();
    pooled_b = pool->Acquire();
    if (!pooled_a || !pooled_b) {
        return 12;
    }
    const uintptr_t first =
        reinterpret_cast<uintptr_t>(pooled_a->MutableData());
    const uintptr_t second =
        reinterpret_cast<uintptr_t>(pooled_b->MutableData());
    const uintptr_t diff = first > second ? first - second : second - first;
    if (first == 0U || second == 0U || diff != 8U) {
        return 13;
    }

    if (CreateMediaBuffer(0) || CreateFixedMediaBufferPool(0, 2)) {
        return 14;
    }

    return 0;
}
