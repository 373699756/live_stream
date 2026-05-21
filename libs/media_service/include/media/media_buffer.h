#ifndef LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_
#define LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_

#include <cstdint>
#include <memory>

namespace live_stream {

using MediaBufferReleaseCallback = void (*)(uint8_t* data, uint32_t capacity,
                                            void* user);

// VideoBuffer owns one contiguous media payload reference. Callers that store a
// pointer beyond the current scope must call VideoBufferRetain() and later
// VideoBufferRelease(). If release is null, the final release frees data with
// free(); otherwise release must release the external mapping or pool block.
struct VideoBuffer {
    uint8_t* data = nullptr;
    uint32_t capacity = 0;
    uint32_t size = 0;
    uint32_t ref_count = 0;
    MediaBufferReleaseCallback release = nullptr;
    void* user = nullptr;
};

struct BufferSlice {
    const VideoBuffer* buffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct MediaBufferPoolStats {
    uint32_t block_size = 0;
    uint32_t block_count = 0;
    uint32_t free_count = 0;
    uint32_t in_use_count = 0;
    uint32_t high_water_count = 0;
    uint64_t no_memory_count = 0;
};

class IVideoBufferPool {
public:
    virtual ~IVideoBufferPool() = default;

    virtual VideoBuffer* Acquire() = 0;
    virtual MediaBufferPoolStats Stats() const = 0;
};

VideoBuffer* VideoBufferAlloc(uint32_t capacity);
VideoBuffer* VideoBufferCreateExternal(uint8_t* data, uint32_t capacity,
                                       uint32_t size,
                                       MediaBufferReleaseCallback release,
                                       void* user);
VideoBuffer* VideoBufferRetain(VideoBuffer* buffer);
bool VideoBufferSetSize(VideoBuffer* buffer, uint32_t size);
void VideoBufferRelease(VideoBuffer* buffer);

std::unique_ptr<IVideoBufferPool> CreateVideoBufferPool(uint32_t block_size,
                                                        uint32_t block_count);

inline bool IsValidBufferSlice(const BufferSlice& slice) {
    if (slice.buffer == nullptr) {
        return false;
    }
    if (slice.offset > slice.buffer->size) {
        return false;
    }
    return slice.size <= slice.buffer->size - slice.offset;
}

inline const uint8_t *BufferSliceData(const BufferSlice& slice) {
    return IsValidBufferSlice(slice) ? slice.buffer->data + slice.offset
                                     : nullptr;
}

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_
