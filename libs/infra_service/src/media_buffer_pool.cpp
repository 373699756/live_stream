#include "infra/media_buffer.h"

#include <mutex>
#include <utility>
#include <vector>

namespace infra {
namespace {

struct FixedPoolState {
    explicit FixedPoolState(uint32_t block_capacity, uint32_t block_total)
        : block_size(block_capacity),
          block_count(block_total),
          blocks(block_total, std::vector<uint8_t>(block_capacity)),
          in_use(block_total, false) {
        free_indices.reserve(block_total);
        for (uint32_t i = 0; i < block_total; ++i) {
            free_indices.push_back(block_total - 1U - i);
        }
    }

    uint32_t block_size = 0;
    uint32_t block_count = 0;
    std::vector<std::vector<uint8_t>> blocks;
    std::vector<bool> in_use;
    std::vector<uint32_t> free_indices;
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
        if (!state_ || index_ >= state_->blocks.size()) {
            return nullptr;
        }
        return state_->blocks[index_].data();
    }

    const uint8_t* Data() const override {
        if (!state_ || index_ >= state_->blocks.size()) {
            return nullptr;
        }
        return state_->blocks[index_].data();
    }

    uint32_t Size() const override { return size_; }

    uint32_t Capacity() const override {
        return state_ ? state_->block_size : 0;
    }

    void SetSize(uint32_t size) override {
        const uint32_t capacity = Capacity();
        size_ = size <= capacity ? size : capacity;
    }

 private:
    void Release() {
        if (!state_) {
            return;
        }
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (index_ < state_->in_use.size() && state_->in_use[index_]) {
            state_->in_use[index_] = false;
            state_->free_indices.push_back(index_);
        }
        state_.reset();
    }

    std::shared_ptr<FixedPoolState> state_;
    uint32_t index_;
    uint32_t size_;
};

class FixedMediaBufferPool final : public IMediaBufferPool {
 public:
    FixedMediaBufferPool(uint32_t block_size, uint32_t block_count)
        : state_(new FixedPoolState(block_size, block_count)) {}

    std::shared_ptr<IMediaBuffer> Acquire() override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->free_indices.empty()) {
            ++state_->no_memory_count;
            return std::shared_ptr<IMediaBuffer>();
        }

        const uint32_t index = state_->free_indices.back();
        state_->free_indices.pop_back();
        state_->in_use[index] = true;
        const uint32_t in_use_count =
            state_->block_count -
            static_cast<uint32_t>(state_->free_indices.size());
        if (in_use_count > state_->high_water_count) {
            state_->high_water_count = in_use_count;
        }
        return std::shared_ptr<IMediaBuffer>(
            new PoolMediaBuffer(state_, index));
    }

    MediaBufferPoolStats Stats() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        MediaBufferPoolStats stats;
        stats.block_size = state_->block_size;
        stats.block_count = state_->block_count;
        stats.free_count = static_cast<uint32_t>(state_->free_indices.size());
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
        return std::shared_ptr<IMediaBufferPool>();
    }
    return std::shared_ptr<IMediaBufferPool>(
        new FixedMediaBufferPool(block_size, block_count));
}

}  // namespace infra
