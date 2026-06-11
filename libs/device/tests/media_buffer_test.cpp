#include "media/encoded_frame.h"

#include <cstdint>
#include <cstring>
#include <memory>

using live_stream::Codec;
using live_stream::CreateFrameBufferPool;
using live_stream::EncodedFrame;
using live_stream::EncodedFramePayloadSlice;
using live_stream::FrameBuffer;
using live_stream::FrameBufferAlloc;
using live_stream::FrameBufferRef;
using live_stream::FrameBufferSetSize;
using live_stream::FrameBufferUnref;
using live_stream::FrameType;
using live_stream::IFrameBufferPool;
using live_stream::IsValidFrameSlice;
using live_stream::MediaBufferPoolStats;
using live_stream::StreamId;

int main() {
    FrameBuffer* buffer = FrameBufferAlloc(16);
    if (buffer == nullptr || buffer->capacity != 16U || buffer->size != 0U) {
        return 1;
    }

    const char* payload = "frame";
    std::memcpy(buffer->data, payload, std::strlen(payload));
    if (!FrameBufferSetSize(buffer,
                            static_cast<uint32_t>(std::strlen(payload)))) {
        return 2;
    }
    if (buffer->size != 5U ||
        std::memcmp(buffer->data, payload, std::strlen(payload)) != 0) {
        return 3;
    }
    if (FrameBufferSetSize(buffer, buffer->capacity + 1U) ||
        buffer->size != 5U) {
        return 4;
    }

    EncodedFrame frame;
    frame.stream_id = StreamId::kMain;
    frame.codec = Codec::kH265;
    frame.frame_type = FrameType::kIdr;
    frame.sequence = 9;
    frame.pts_us = 100;
    frame.dts_us = 90;
    frame.buffer = FrameBufferRef(buffer);
    frame.offset = 0;
    frame.size = buffer->size;

    if (buffer->ref_count != 2U || frame.size != 5U ||
        frame.codec != Codec::kH265) {
        return 5;
    }
    if (!IsValidFrameSlice(EncodedFramePayloadSlice(&frame))) {
        return 6;
    }

    FrameBufferUnref(buffer);

    std::unique_ptr<IFrameBufferPool> pool = CreateFrameBufferPool(8, 2);
    if (!pool) {
        return 7;
    }
    FrameBuffer* pooled_a = pool->Acquire();
    FrameBuffer* pooled_b = pool->Acquire();
    FrameBuffer* pooled_c = pool->Acquire();
    if (pooled_a == nullptr || pooled_b == nullptr || pooled_c != nullptr) {
        return 8;
    }
    MediaBufferPoolStats stats = pool->Stats();
    if (stats.in_use_count != 2U || stats.free_count != 0U ||
        stats.no_memory_count != 1U) {
        return 9;
    }
    FrameBufferUnref(pooled_a);
    pooled_a = nullptr;
    stats = pool->Stats();
    if (stats.in_use_count != 1U || stats.free_count != 1U) {
        return 10;
    }
    FrameBufferUnref(pooled_b);
    pooled_b = nullptr;
    stats = pool->Stats();
    if (stats.in_use_count != 0U || stats.free_count != 2U ||
        stats.high_water_count != 2U) {
        return 11;
    }

    pooled_a = pool->Acquire();
    pooled_b = pool->Acquire();
    if (pooled_a == nullptr || pooled_b == nullptr) {
        return 12;
    }
    const uintptr_t first =
        reinterpret_cast<uintptr_t>(pooled_a->data);
    const uintptr_t second =
        reinterpret_cast<uintptr_t>(pooled_b->data);
    const uintptr_t diff = first > second ? first - second : second - first;
    if (first == 0U || second == 0U || diff != 8U) {
        return 13;
    }

    std::unique_ptr<IFrameBufferPool> temp_pool = CreateFrameBufferPool(8, 1);
    FrameBuffer* outstanding = temp_pool->Acquire();
    temp_pool.reset();
    FrameBufferUnref(outstanding);

    FrameBufferUnref(pooled_a);
    FrameBufferUnref(pooled_b);

    if (FrameBufferAlloc(0) != nullptr || CreateFrameBufferPool(0, 2)) {
        return 14;
    }

    return 0;
}
