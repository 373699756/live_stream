#ifndef LIVE_STREAM_MEDIA_SRC_MEDIA_BUFFER_POOL_BLOCKS_H_
#define LIVE_STREAM_MEDIA_SRC_MEDIA_BUFFER_POOL_BLOCKS_H_

#include "media/media_buffer.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace live_stream {

class MediaBufferPoolBlocks {
public:
    MediaBufferPoolBlocks(uint32_t block_bytes, uint32_t pool_size);
    MediaBufferPoolBlocks(const MediaBufferPoolBlocks &other) = delete;
    MediaBufferPoolBlocks &operator=(const MediaBufferPoolBlocks &other) =
        delete;

    bool Allocate();
    void AddRef();
    void ReleaseRef();
    void ReturnBlock(uint32_t index);

    MediaBufferBuilder Acquire();
    MediaBufferPoolStats Info() const;

private:
    ~MediaBufferPoolBlocks();

    void ReleaseBlockIndexLocked(uint32_t index);

    uint32_t block_bytes_ = 0;
    uint32_t pool_size_ = 0;
    uint32_t free_size_ = 0;
    uint8_t *data_ = nullptr;
    uint8_t *in_use_ = nullptr;
    uint32_t *free_indices_ = nullptr;
    uint32_t high_water_size_ = 0;
    uint64_t allocation_failures_ = 0;
    std::atomic<uint32_t> refs_{1};
    mutable std::mutex mutex_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_MEDIA_BUFFER_POOL_BLOCKS_H_
