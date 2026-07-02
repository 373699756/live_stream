#include "media_buffer_pool_blocks.h"

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

namespace live_stream {
namespace {

struct MediaBufferPoolBlockRef {
    MediaBufferPoolBlocks *blocks = nullptr;
    uint32_t index = 0;
};

void FreePoolBlock(uint8_t *data, uint32_t capacity, void *user) {
    (void)data;
    (void)capacity;
    MediaBufferPoolBlockRef *ref =
        static_cast<MediaBufferPoolBlockRef *>(user);
    if (ref == nullptr || ref->blocks == nullptr) {
        std::free(ref);
        return;
    }
    ref->blocks->ReturnBlock(ref->index);
    ref->blocks->ReleaseRef();
    std::free(ref);
}

}  // namespace

MediaBufferPoolBlocks::MediaBufferPoolBlocks(uint32_t block_bytes,
                                             uint32_t pool_size)
    : block_bytes_(block_bytes),
      pool_size_(pool_size),
      free_size_(pool_size) {}

MediaBufferPoolBlocks::~MediaBufferPoolBlocks() {
    std::free(data_);
    std::free(in_use_);
    std::free(free_indices_);
}

bool MediaBufferPoolBlocks::Allocate() {
    if (block_bytes_ == 0 || pool_size_ == 0) {
        return false;
    }
    const std::size_t max_size = std::numeric_limits<std::size_t>::max();
    if (static_cast<std::size_t>(pool_size_) >
        max_size / static_cast<std::size_t>(block_bytes_)) {
        return false;
    }

    const std::size_t total_size =
        static_cast<std::size_t>(block_bytes_) * pool_size_;
    data_ = static_cast<uint8_t *>(std::malloc(total_size));
    in_use_ = static_cast<uint8_t *>(std::malloc(pool_size_));
    free_indices_ =
        static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * pool_size_));
    if (data_ == nullptr || in_use_ == nullptr || free_indices_ == nullptr) {
        return false;
    }
    for (uint32_t i = 0; i < pool_size_; ++i) {
        in_use_[i] = 0;
        free_indices_[i] = pool_size_ - 1U - i;
    }
    return true;
}

void MediaBufferPoolBlocks::AddRef() {
    (void)refs_.fetch_add(1, std::memory_order_relaxed);
}

void MediaBufferPoolBlocks::ReleaseRef() {
    if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete this;
    }
}

MediaBufferBuilder MediaBufferPoolBlocks::Acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (free_size_ == 0) {
        ++allocation_failures_;
        return MediaBufferBuilder();
    }

    --free_size_;
    const uint32_t index = free_indices_[free_size_];
    in_use_[index] = 1;
    const uint32_t in_use_size = pool_size_ - free_size_;
    if (in_use_size > high_water_size_) {
        high_water_size_ = in_use_size;
    }

    MediaBufferPoolBlockRef *ref = static_cast<MediaBufferPoolBlockRef *>(
        std::malloc(sizeof(MediaBufferPoolBlockRef)));
    if (ref == nullptr) {
        ReleaseBlockIndexLocked(index);
        ++allocation_failures_;
        return MediaBufferBuilder();
    }
    ref->blocks = this;
    ref->index = index;
    AddRef();

    uint8_t *block =
        data_ + static_cast<std::size_t>(index) * block_bytes_;
    MediaBufferBuilder buffer = MediaBufferBuilder::WrapExternalMemory(
        block, block_bytes_, 0, FreePoolBlock, ref);
    if (!buffer.Valid()) {
        std::free(ref);
        ReleaseBlockIndexLocked(index);
        ++allocation_failures_;
        ReleaseRef();
        return MediaBufferBuilder();
    }
    return buffer;
}

MediaBufferPoolStats MediaBufferPoolBlocks::Stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    MediaBufferPoolStats info;
    info.block_bytes = block_bytes_;
    info.pool_size = pool_size_;
    info.free_size = free_size_;
    info.in_use_size = pool_size_ - info.free_size;
    info.high_water_size = high_water_size_;
    info.allocation_failures = allocation_failures_;
    return info;
}

void MediaBufferPoolBlocks::ReturnBlock(uint32_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < pool_size_ && in_use_[index] != 0) {
        ReleaseBlockIndexLocked(index);
    }
}

void MediaBufferPoolBlocks::ReleaseBlockIndexLocked(uint32_t index) {
    in_use_[index] = 0;
    free_indices_[free_size_] = index;
    ++free_size_;
}

}  // namespace live_stream
