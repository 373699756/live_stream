#include "infra/media_buffer.h"

#include <vector>

namespace infra {
namespace {

class MediaBufferImpl : public IMediaBuffer {
 public:
    explicit MediaBufferImpl(uint32_t capacity) : data_(capacity), size_(0) {}

    uint8_t* MutableData() override { return data_.empty() ? nullptr : data_.data(); }
    const uint8_t* Data() const override {
        return data_.empty() ? nullptr : data_.data();
    }
    uint32_t Size() const override { return size_; }
    uint32_t Capacity() const override {
        return static_cast<uint32_t>(data_.size());
    }
    void SetSize(uint32_t size) override {
        size_ = size <= Capacity() ? size : Capacity();
    }

 private:
    std::vector<uint8_t> data_;
    uint32_t size_;
};

}  // namespace

std::shared_ptr<IMediaBuffer> CreateMediaBuffer(uint32_t capacity) {
    if (capacity == 0) {
        return std::shared_ptr<IMediaBuffer>();
    }
    return std::shared_ptr<IMediaBuffer>(new MediaBufferImpl(capacity));
}

bool IsValidBufferSlice(const BufferSlice& slice) {
    if (!slice.buffer) {
        return false;
    }
    if (slice.offset > slice.buffer->Size()) {
        return false;
    }
    return slice.size <= slice.buffer->Size() - slice.offset;
}

}  // namespace infra
