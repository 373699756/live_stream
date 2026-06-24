#include "media/media_buffer.h"

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <new>

namespace live_stream {
namespace {

struct PoolState {
    PoolState(uint32_t block_capacity, uint32_t pool_capacity)
        : block_bytes(block_capacity),
          pool_size(pool_capacity),
          free_size(pool_capacity) {}

    ~PoolState() {
        std::free(data);
        std::free(in_use);
        std::free(free_indices);
    }

    uint32_t block_bytes = 0;
    uint32_t pool_size = 0;
    uint32_t free_size = 0;
    uint8_t* data = nullptr;
    uint8_t* in_use = nullptr;
    uint32_t* free_indices = nullptr;
    uint32_t high_water_size = 0;
    uint64_t allocation_failures = 0;
    uint32_t refs = 1;
    std::mutex mutex;
};

struct PoolBlockRef {
    PoolState* state = nullptr;
    uint32_t index = 0;
};

void AddPoolStateRef(PoolState* state) {
    if (state != nullptr) {
        (void)__sync_add_and_fetch(&state->refs, 1);
    }
}

void ReleasePoolState(PoolState* state) {
    if (state == nullptr) {
        return;
    }
    if (__sync_sub_and_fetch(&state->refs, 1) == 0) {
        delete state;
    }
}

void FreePoolBlock(uint8_t* data, uint32_t capacity, void* user) {
    (void)data;
    (void)capacity;
    PoolBlockRef* ref = static_cast<PoolBlockRef*>(user);
    if (ref == nullptr || ref->state == nullptr) {
        std::free(ref);
        return;
    }
    PoolState* state = ref->state;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (ref->index < state->pool_size && state->in_use[ref->index] != 0) {
            state->in_use[ref->index] = 0;
            state->free_indices[state->free_size] = ref->index;
            ++state->free_size;
        }
    }
    std::free(ref);
    ReleasePoolState(state);
}

class MediaBufferPool final : public IMediaBufferPool {
public:
    explicit MediaBufferPool(PoolState* state) : state_(state) {}

    ~MediaBufferPool() override { ReleasePoolState(state_); }

    MediaBufferBuilder Acquire() override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->free_size == 0) {
            ++state_->allocation_failures;
            return MediaBufferBuilder();
        }

        --state_->free_size;
        const uint32_t index = state_->free_indices[state_->free_size];
        state_->in_use[index] = 1;
        const uint32_t in_use_size =
            state_->pool_size - state_->free_size;
        if (in_use_size > state_->high_water_size) {
            state_->high_water_size = in_use_size;
        }

        PoolBlockRef* ref =
            static_cast<PoolBlockRef*>(std::malloc(sizeof(PoolBlockRef)));
        if (ref == nullptr) {
            ReleaseBlockIndexLocked(index);
            ++state_->allocation_failures;
            return MediaBufferBuilder();
        }
        ref->state = state_;
        ref->index = index;
        AddPoolStateRef(state_);
        uint8_t* block = state_->data +
                         static_cast<std::size_t>(index) * state_->block_bytes;
        MediaBufferBuilder buffer = MediaBufferBuilder::WrapExternalMemory(
            block, state_->block_bytes, 0, FreePoolBlock, ref);
        if (!buffer.Valid()) {
            std::free(ref);
            ReleaseBlockIndexLocked(index);
            ++state_->allocation_failures;
            ReleasePoolState(state_);
            return MediaBufferBuilder();
        }
        return buffer;
    }

    MediaBufferPoolStats Stats() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        MediaBufferPoolStats stats;
        stats.block_bytes = state_->block_bytes;
        stats.pool_size = state_->pool_size;
        stats.free_size = state_->free_size;
        stats.in_use_size = state_->pool_size - stats.free_size;
        stats.high_water_size = state_->high_water_size;
        stats.allocation_failures = state_->allocation_failures;
        return stats;
    }

private:
    void ReleaseBlockIndexLocked(uint32_t index) {
        state_->in_use[index] = 0;
        state_->free_indices[state_->free_size] = index;
        ++state_->free_size;
    }

    PoolState* state_ = nullptr;
};

}  // namespace

std::unique_ptr<IMediaBufferPool> CreateMediaBufferPool(uint32_t block_bytes,
                                                        uint32_t pool_size) {
    if (block_bytes == 0 || pool_size == 0) {
        return nullptr;
    }
    const std::size_t max_size = std::numeric_limits<std::size_t>::max();
    if (static_cast<std::size_t>(pool_size) >
        max_size / static_cast<std::size_t>(block_bytes)) {
        return nullptr;
    }

    PoolState* state = new (std::nothrow) PoolState(block_bytes, pool_size);
    if (state == nullptr) {
        return nullptr;
    }
    const std::size_t total_size =
        static_cast<std::size_t>(block_bytes) * pool_size;
    state->data = static_cast<uint8_t*>(std::malloc(total_size));
    state->in_use = static_cast<uint8_t*>(std::malloc(pool_size));
    state->free_indices =
        static_cast<uint32_t*>(std::malloc(sizeof(uint32_t) * pool_size));
    if (state->data == nullptr || state->in_use == nullptr ||
        state->free_indices == nullptr) {
        ReleasePoolState(state);
        return nullptr;
    }
    for (uint32_t i = 0; i < pool_size; ++i) {
        state->in_use[i] = 0;
        state->free_indices[i] = pool_size - 1U - i;
    }
    std::unique_ptr<IMediaBufferPool> pool(
        new (std::nothrow) MediaBufferPool(state));
    if (!pool) {
        ReleasePoolState(state);
        return nullptr;
    }
    return pool;
}

}  // namespace live_stream
