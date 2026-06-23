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
    PoolState(uint32_t block_capacity, uint32_t block_total)
        : block_size(block_capacity),
          block_count(block_total),
          free_count(block_total) {}

    ~PoolState() {
        std::free(data);
        std::free(in_use);
        std::free(free_indices);
    }

    uint32_t block_size = 0;
    uint32_t block_count = 0;
    uint32_t free_count = 0;
    uint8_t* data = nullptr;
    uint8_t* in_use = nullptr;
    uint32_t* free_indices = nullptr;
    uint32_t high_water_count = 0;
    uint64_t no_memory_count = 0;
    uint32_t ref_count = 1;
    std::mutex mutex;
};

struct PoolBlockRef {
    PoolState* state = nullptr;
    uint32_t index = 0;
};

void PoolStateRef(PoolState* state) {
    if (state != nullptr) {
        (void)__sync_add_and_fetch(&state->ref_count, 1);
    }
}

void PoolStateUnref(PoolState* state) {
    if (state == nullptr) {
        return;
    }
    if (__sync_sub_and_fetch(&state->ref_count, 1) == 0) {
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
        if (ref->index < state->block_count && state->in_use[ref->index] != 0) {
            state->in_use[ref->index] = 0;
            state->free_indices[state->free_count] = ref->index;
            ++state->free_count;
        }
    }
    std::free(ref);
    PoolStateUnref(state);
}

class MediaBufferPool final : public IMediaBufferPool {
public:
    explicit MediaBufferPool(PoolState* state) : state_(state) {}

    ~MediaBufferPool() override { PoolStateUnref(state_); }

    MediaBufferRef Acquire() override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->free_count == 0) {
            ++state_->no_memory_count;
            return MediaBufferRef();
        }

        --state_->free_count;
        const uint32_t index = state_->free_indices[state_->free_count];
        state_->in_use[index] = 1;
        const uint32_t in_use_count =
            state_->block_count - state_->free_count;
        if (in_use_count > state_->high_water_count) {
            state_->high_water_count = in_use_count;
        }

        PoolBlockRef* ref =
            static_cast<PoolBlockRef*>(std::malloc(sizeof(PoolBlockRef)));
        if (ref == nullptr) {
            UnrefIndexLocked(index);
            ++state_->no_memory_count;
            return MediaBufferRef();
        }
        ref->state = state_;
        ref->index = index;
        PoolStateRef(state_);
        uint8_t* block = state_->data +
                         static_cast<std::size_t>(index) * state_->block_size;
        MediaBufferRef buffer = MediaBufferRef::AdoptExternal(
            block, state_->block_size, 0, FreePoolBlock, ref);
        if (!buffer.RawOwner()) {
            std::free(ref);
            UnrefIndexLocked(index);
            ++state_->no_memory_count;
            PoolStateUnref(state_);
            return MediaBufferRef();
        }
        return buffer;
    }

    MediaBufferPoolStats Stats() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        MediaBufferPoolStats stats;
        stats.block_size = state_->block_size;
        stats.block_count = state_->block_count;
        stats.free_count = state_->free_count;
        stats.in_use_count = state_->block_count - stats.free_count;
        stats.high_water_count = state_->high_water_count;
        stats.no_memory_count = state_->no_memory_count;
        return stats;
    }

private:
    void UnrefIndexLocked(uint32_t index) {
        state_->in_use[index] = 0;
        state_->free_indices[state_->free_count] = index;
        ++state_->free_count;
    }

    PoolState* state_ = nullptr;
};

}  // namespace

std::unique_ptr<IMediaBufferPool> CreateMediaBufferPool(uint32_t block_size,
                                                        uint32_t block_count) {
    if (block_size == 0 || block_count == 0) {
        return nullptr;
    }
    const std::size_t max_size = std::numeric_limits<std::size_t>::max();
    if (static_cast<std::size_t>(block_count) >
        max_size / static_cast<std::size_t>(block_size)) {
        return nullptr;
    }

    PoolState* state = new (std::nothrow) PoolState(block_size, block_count);
    if (state == nullptr) {
        return nullptr;
    }
    const std::size_t total_size =
        static_cast<std::size_t>(block_size) * block_count;
    state->data = static_cast<uint8_t*>(std::malloc(total_size));
    state->in_use = static_cast<uint8_t*>(std::malloc(block_count));
    state->free_indices =
        static_cast<uint32_t*>(std::malloc(sizeof(uint32_t) * block_count));
    if (state->data == nullptr || state->in_use == nullptr ||
        state->free_indices == nullptr) {
        PoolStateUnref(state);
        return nullptr;
    }
    for (uint32_t i = 0; i < block_count; ++i) {
        state->in_use[i] = 0;
        state->free_indices[i] = block_count - 1U - i;
    }
    std::unique_ptr<IMediaBufferPool> pool(
        new (std::nothrow) MediaBufferPool(state));
    if (!pool) {
        PoolStateUnref(state);
        return nullptr;
    }
    return pool;
}

}  // namespace live_stream
