#ifndef LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_
#define LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <memory>

namespace live_stream {

using MediaBufferFreeCallback = void (*)(uint8_t* data, uint32_t capacity,
                                         void* user);

struct MediaBuffer;
class MediaBufferRef;

class MediaBufferRef {
public:
    class Builder;

    MediaBufferRef() = default;
    MediaBufferRef(const MediaBufferRef& other);
    MediaBufferRef& operator=(const MediaBufferRef& other);
    MediaBufferRef(MediaBufferRef&& other) noexcept;
    MediaBufferRef& operator=(MediaBufferRef&& other) noexcept;
    ~MediaBufferRef();

    MediaBufferRef Slice(uint32_t offset, uint32_t size) const;

    const uint8_t* Data() const;
    uint32_t Size() const;
    uint32_t Capacity() const;
    bool Valid() const;
    void Reset();

private:
    explicit MediaBufferRef(MediaBuffer* buffer);
    MediaBufferRef(MediaBuffer* buffer, uint32_t offset, uint32_t size);
    static MediaBufferRef WrapExternalMemory(
        uint8_t* data, uint32_t capacity, uint32_t size,
        MediaBufferFreeCallback free_callback, void* user);
    bool SetSize(uint32_t size);

    MediaBuffer* buffer_ = nullptr;
    uint32_t offset_ = 0;
    uint32_t size_ = 0;
};

class MediaBufferRef::Builder {
public:
    Builder() = default;
    Builder(const Builder& other) = delete;
    Builder& operator=(const Builder& other) = delete;
    Builder(Builder&& other) noexcept = default;
    Builder& operator=(Builder&& other) noexcept = default;
    ~Builder() = default;

    static Builder Allocate(uint32_t capacity);
    // Wraps caller-owned writable memory while it is being filled. On failure,
    // ownership remains with the caller.
    static Builder WrapExternalMemory(
        uint8_t* data, uint32_t capacity, uint32_t size,
        MediaBufferFreeCallback free_callback, void* user);

    uint8_t* Data();
    const uint8_t* Data() const;
    uint32_t Size() const;
    uint32_t Capacity() const;
    bool Resize(uint32_t size);
    bool Valid() const;
    MediaBufferRef Finish();
    void Reset();

private:
    explicit Builder(MediaBufferRef buffer);

    MediaBufferRef buffer_;
};

using MediaBufferBuilder = MediaBufferRef::Builder;

struct MediaOutSlice {
    const uint8_t* data = nullptr;
    size_t size = 0;
    MediaBufferRef buffer;
};

struct MediaBufferPoolStats {
    uint32_t block_bytes = 0;
    uint32_t pool_size = 0;
    uint32_t free_size = 0;
    uint32_t in_use_size = 0;
    uint32_t high_water_size = 0;
    uint64_t allocation_failures = 0;
};

class IMediaBufferPool {
public:
    virtual ~IMediaBufferPool() = default;

    virtual MediaBufferBuilder Acquire() = 0;
    virtual MediaBufferPoolStats Stats() const = 0;
};

std::unique_ptr<IMediaBufferPool> CreateMediaBufferPool(uint32_t block_bytes,
                                                        uint32_t pool_size);

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_
