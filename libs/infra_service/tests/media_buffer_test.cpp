#include "infra/encoded_frame.h"

#include <cstdint>
#include <cstring>

int main() {
    std::shared_ptr<infra::IMediaBuffer> buffer = infra::CreateMediaBuffer(16);
    if (!buffer || buffer->Capacity() != 16U || buffer->Size() != 0U) {
        return 1;
    }

    const char* payload = "frame";
    std::memcpy(buffer->MutableData(), payload, std::strlen(payload));
    buffer->SetSize(static_cast<uint32_t>(std::strlen(payload)));
    if (buffer->Size() != 5U ||
        std::memcmp(buffer->Data(), payload, std::strlen(payload)) != 0) {
        return 2;
    }

    infra::EncodedFrame frame;
    frame.stream_id = infra::StreamId::kMain;
    frame.codec = infra::VideoCodec::kH265;
    frame.frame_type = infra::FrameType::kIdr;
    frame.sequence = 9;
    frame.pts_us = 100;
    frame.dts_us = 90;
    frame.buffer = buffer;
    frame.offset = 0;
    frame.size = buffer->Size();

    if (frame.buffer.use_count() != 2 || frame.size != 5U ||
        frame.codec != infra::VideoCodec::kH265) {
        return 3;
    }
    if (!infra::IsValidBufferSlice(frame.PayloadSlice())) {
        return 4;
    }

    std::shared_ptr<infra::IMediaBufferPool> pool =
        infra::CreateFixedMediaBufferPool(8, 2);
    if (!pool) {
        return 5;
    }
    std::shared_ptr<infra::IMediaBuffer> pooled_a = pool->Acquire();
    std::shared_ptr<infra::IMediaBuffer> pooled_b = pool->Acquire();
    std::shared_ptr<infra::IMediaBuffer> pooled_c = pool->Acquire();
    if (!pooled_a || !pooled_b || pooled_c) {
        return 6;
    }
    infra::MediaBufferPoolStats stats = pool->Stats();
    if (stats.in_use_count != 2U || stats.free_count != 0U ||
        stats.no_memory_count != 1U) {
        return 7;
    }
    pooled_a.reset();
    stats = pool->Stats();
    if (stats.in_use_count != 1U || stats.free_count != 1U) {
        return 8;
    }
    pooled_b.reset();
    stats = pool->Stats();
    if (stats.in_use_count != 0U || stats.free_count != 2U ||
        stats.high_water_count != 2U) {
        return 9;
    }

    pooled_a = pool->Acquire();
    pooled_b = pool->Acquire();
    if (!pooled_a || !pooled_b) {
        return 10;
    }
    const uintptr_t first =
        reinterpret_cast<uintptr_t>(pooled_a->MutableData());
    const uintptr_t second =
        reinterpret_cast<uintptr_t>(pooled_b->MutableData());
    const uintptr_t diff = first > second ? first - second : second - first;
    if (first == 0U || second == 0U || diff != 8U) {
        return 11;
    }

    return 0;
}
