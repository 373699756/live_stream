#include "media/media_buffer.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

namespace live_stream {
namespace {

struct FixedPoolState {
    explicit FixedPoolState(uint32_t block_capacity, uint32_t block_total)
        : block_size(block_capacity),
          block_count(block_total),
          free_count(block_total) {}

    uint32_t block_size = 0;
    uint32_t block_count = 0;
    uint32_t free_count = 0;
    std::unique_ptr<uint8_t[]> data;
    std::unique_ptr<uint8_t[]> in_use;
    std::unique_ptr<uint32_t[]> free_indices;
    uint32_t high_water_count = 0;
    uint64_t no_memory_count = 0;
    mutable std::mutex mutex;
};

class PoolMediaBuffer final : public IMediaBuffer {
public:
    PoolMediaBuffer(std::shared_ptr<FixedPoolState> state, uint32_t index)
        : state_(std::move(state)), index_(index), size_(0) {}

    ~PoolMediaBuffer() override { Release(); }

    uint8_t* MutableData() override {
        if (!state_ || index_ >= state_->block_count) {
            return nullptr;
        }
        return state_->data.get() +
               static_cast<std::size_t>(index_) * state_->block_size;
    }

    const uint8_t* Data() const override {
        if (!state_ || index_ >= state_->block_count) {
            return nullptr;
        }
        return state_->data.get() +
               static_cast<std::size_t>(index_) * state_->block_size;
    }

    uint32_t Size() const override { return size_; }
    uint32_t Capacity() const override { return state_ ? state_->block_size : 0; }

    bool SetSize(uint32_t size) override {
        const uint32_t capacity = Capacity();
        if (size > capacity) {
            return false;
        }
        size_ = size;
        return true;
    }

private:
    void Release() {
        if (!state_) {
            return;
        }
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (index_ < state_->block_count && state_->in_use[index_] != 0) {
            state_->in_use[index_] = 0;
            state_->free_indices[state_->free_count] = index_;
            ++state_->free_count;
        }
        state_.reset();
    }

    std::shared_ptr<FixedPoolState> state_;
    uint32_t index_;
    uint32_t size_;
};

class FixedMediaBufferPool final : public IMediaBufferPool {
public:
    explicit FixedMediaBufferPool(std::shared_ptr<FixedPoolState> state)
        : state_(std::move(state)) {}

    std::shared_ptr<IMediaBuffer> Acquire() override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->free_count == 0) {
            ++state_->no_memory_count;
            return nullptr;
        }

        --state_->free_count;
        const uint32_t index = state_->free_indices[state_->free_count];
        state_->in_use[index] = 1;
        const uint32_t in_use_count = state_->block_count - state_->free_count;
        if (in_use_count > state_->high_water_count) {
            state_->high_water_count = in_use_count;
        }
        std::shared_ptr<IMediaBuffer> buffer(
            new (std::nothrow) PoolMediaBuffer(state_, index));
        if (!buffer) {
            state_->in_use[index] = 0;
            state_->free_indices[state_->free_count] = index;
            ++state_->free_count;
            ++state_->no_memory_count;
            return nullptr;
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
    std::shared_ptr<FixedPoolState> state_;
};

}  // namespace

std::shared_ptr<IMediaBufferPool> CreateFixedMediaBufferPool(
    uint32_t block_size, uint32_t block_count) {
    if (block_size == 0 || block_count == 0) {
        return nullptr;
    }
    const std::size_t max_size = std::numeric_limits<std::size_t>::max();
    if (static_cast<std::size_t>(block_count) >
        max_size / static_cast<std::size_t>(block_size)) {
        return nullptr;
    }
    std::shared_ptr<FixedPoolState> state(
        new (std::nothrow) FixedPoolState(block_size, block_count));
    if (!state) {
        return nullptr;
    }
    const std::size_t total_size =
        static_cast<std::size_t>(block_size) * block_count;
    state->data.reset(new (std::nothrow) uint8_t[total_size]);
    state->in_use.reset(new (std::nothrow) uint8_t[block_count]);
    state->free_indices.reset(new (std::nothrow) uint32_t[block_count]);
    if (!state->data || !state->in_use || !state->free_indices) {
        return nullptr;
    }
    for (uint32_t i = 0; i < block_count; ++i) {
        state->in_use[i] = 0;
        state->free_indices[i] = block_count - 1U - i;
    }
    return std::shared_ptr<IMediaBufferPool>(
        new (std::nothrow) FixedMediaBufferPool(state));
}

}  // namespace live_stream
