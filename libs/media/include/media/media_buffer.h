#ifndef LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_
#define LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <memory>

namespace live_stream {

using MediaBufferFreeCallback = void (*)(uint8_t* data, uint32_t capacity,
                                         void* user);

// MediaBuffer owns one encoded media payload allocation. Public code should
// keep it alive through MediaBufferRef instead of manually ref/unrefing it.
struct MediaBuffer {
    uint8_t* data = nullptr;
    uint32_t capacity = 0;
    uint32_t size = 0;
    uint32_t ref_count = 0;
    MediaBufferFreeCallback free_callback = nullptr;
    void* user = nullptr;
};

MediaBuffer* MediaBufferAddRef(MediaBuffer* buffer);
void MediaBufferRelease(MediaBuffer* buffer);

class MediaBufferRef {
public:
    MediaBufferRef() = default;
    MediaBufferRef(const MediaBufferRef& other);
    MediaBufferRef& operator=(const MediaBufferRef& other);
    MediaBufferRef(MediaBufferRef&& other) noexcept;
    MediaBufferRef& operator=(MediaBufferRef&& other) noexcept;
    ~MediaBufferRef();

    static MediaBufferRef Allocate(uint32_t capacity);
    static MediaBufferRef AdoptExternal(uint8_t* data, uint32_t capacity,
                                        uint32_t size,
                                        MediaBufferFreeCallback free_callback,
                                        void* user);

    MediaBufferRef Slice(uint32_t offset, uint32_t size) const;

    const uint8_t* Data() const;
    uint8_t* MutableData();
    uint32_t Size() const;
    uint32_t Capacity() const;
    bool SetSize(uint32_t size);
    bool Valid() const;
    void Reset();

    // RawOwner is only for low-level bridge code that must pass the lifetime
    // owner to net's protocol-agnostic send queue.
    MediaBuffer* RawOwner() const { return buffer_; }

private:
    explicit MediaBufferRef(MediaBuffer* buffer);
    MediaBufferRef(MediaBuffer* buffer, uint32_t offset, uint32_t size);

    MediaBuffer* buffer_ = nullptr;
    uint32_t offset_ = 0;
    uint32_t size_ = 0;
};

struct MediaOutSlice {
    const uint8_t* data = nullptr;
    size_t size = 0;
    MediaBufferRef owner;
};

struct MediaBufferPoolStats {
    uint32_t block_size = 0;
    uint32_t block_count = 0;
    uint32_t free_count = 0;
    uint32_t in_use_count = 0;
    uint32_t high_water_count = 0;
    uint64_t no_memory_count = 0;
};

class IMediaBufferPool {
public:
    virtual ~IMediaBufferPool() = default;

    virtual MediaBufferRef Acquire() = 0;
    virtual MediaBufferPoolStats Stats() const = 0;
};

std::unique_ptr<IMediaBufferPool> CreateMediaBufferPool(uint32_t block_size,
                                                        uint32_t block_count);

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_
