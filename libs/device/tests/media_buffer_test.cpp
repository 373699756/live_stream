#include "media/media_frame.h"

#include <cstdint>
#include <cstring>
#include <memory>

using live_stream::Codec;
using live_stream::CreateMediaBufferPool;
using live_stream::MediaFrame;
using live_stream::MediaBufferBuilder;
using live_stream::MediaBufferRef;
using live_stream::FrameType;
using live_stream::IMediaBufferPool;
using live_stream::MediaBufferPoolStats;
using live_stream::StreamId;

int main() {
    MediaBufferBuilder builder = MediaBufferBuilder::Allocate(16);
    if (!builder.Valid() || builder.Capacity() != 16U ||
        builder.Size() != 0U) {
        return 1;
    }

    const char* payload = "frame";
    std::memcpy(builder.Data(), payload, std::strlen(payload));
    if (!builder.Resize(static_cast<uint32_t>(std::strlen(payload)))) {
        return 2;
    }
    if (builder.Resize(builder.Capacity() + 1U) || builder.Size() != 5U) {
        return 4;
    }
    MediaBufferRef buffer = builder.Finish();
    if (buffer.Size() != 5U ||
        std::memcmp(buffer.Data(), payload, std::strlen(payload)) != 0) {
        return 3;
    }

    MediaFrame frame;
    frame.stream_id = StreamId::kMain;
    frame.codec = Codec::kH265;
    frame.frame_type = FrameType::kIdr;
    frame.sequence = 9;
    frame.pts_us = 100;
    frame.dts_us = 90;
    frame.payload = buffer;

    if (frame.payload.Size() != 5U || frame.codec != Codec::kH265) {
        return 5;
    }
    if (!frame.payload.Valid()) {
        return 6;
    }

    std::unique_ptr<IMediaBufferPool> pool = CreateMediaBufferPool(8, 2);
    if (!pool) {
        return 7;
    }
    MediaBufferBuilder pooled_a = pool->Acquire();
    MediaBufferBuilder pooled_b = pool->Acquire();
    MediaBufferBuilder pooled_c = pool->Acquire();
    if (!pooled_a.Valid() || !pooled_b.Valid() || pooled_c.Valid()) {
        return 8;
    }
    MediaBufferPoolStats stats = pool->Stats();
    if (stats.in_use_count != 2U || stats.free_count != 0U ||
        stats.no_memory_count != 1U) {
        return 9;
    }
    pooled_a.Reset();
    stats = pool->Stats();
    if (stats.in_use_count != 1U || stats.free_count != 1U) {
        return 10;
    }
    pooled_b.Reset();
    stats = pool->Stats();
    if (stats.in_use_count != 0U || stats.free_count != 2U ||
        stats.high_water_count != 2U) {
        return 11;
    }

    pooled_a = pool->Acquire();
    pooled_b = pool->Acquire();
    if (!pooled_a.Valid() || !pooled_b.Valid()) {
        return 12;
    }
    const uintptr_t first =
        reinterpret_cast<uintptr_t>(pooled_a.Data());
    const uintptr_t second =
        reinterpret_cast<uintptr_t>(pooled_b.Data());
    const uintptr_t diff = first > second ? first - second : second - first;
    if (first == 0U || second == 0U || diff != 8U) {
        return 13;
    }

    std::unique_ptr<IMediaBufferPool> temp_pool = CreateMediaBufferPool(8, 1);
    MediaBufferBuilder outstanding = temp_pool->Acquire();
    temp_pool.reset();

    if (MediaBufferBuilder::Allocate(0).Valid() ||
        CreateMediaBufferPool(0, 2)) {
        return 14;
    }

    return 0;
}
