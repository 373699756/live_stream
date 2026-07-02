#include "media/media_buffer.h"

#include "media_buffer_pool_blocks.h"

#include <memory>
#include <new>

namespace live_stream {
namespace {

class MediaBufferPool final : public IMediaBufferPool {
public:
    explicit MediaBufferPool(MediaBufferPoolBlocks *blocks)
        : blocks_(blocks) {}

    ~MediaBufferPool() override {
        if (blocks_ != nullptr) {
            blocks_->ReleaseRef();
        }
    }

    MediaBufferBuilder Acquire() override {
        if (blocks_ == nullptr) {
            return MediaBufferBuilder();
        }
        return blocks_->Acquire();
    }

    MediaBufferPoolStats Stats() const override {
        if (blocks_ == nullptr) {
            return MediaBufferPoolStats();
        }
        return blocks_->Stats();
    }

private:
    MediaBufferPoolBlocks *blocks_ = nullptr;
};

}  // namespace

std::unique_ptr<IMediaBufferPool> CreateMediaBufferPool(uint32_t block_bytes,
                                                        uint32_t pool_size) {
    MediaBufferPoolBlocks *blocks =
        new (std::nothrow) MediaBufferPoolBlocks(block_bytes, pool_size);
    if (blocks == nullptr) {
        return nullptr;
    }
    if (!blocks->Allocate()) {
        blocks->ReleaseRef();
        return nullptr;
    }

    std::unique_ptr<IMediaBufferPool> pool(
        new (std::nothrow) MediaBufferPool(blocks));
    if (!pool) {
        blocks->ReleaseRef();
        return nullptr;
    }
    return pool;
}

}  // namespace live_stream
