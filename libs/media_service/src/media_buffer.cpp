#include "media/media_buffer.h"

#include <memory>
#include <new>
#include <utility>

namespace live_stream {
namespace {

class MediaBufferImpl : public IMediaBuffer {
public:
    MediaBufferImpl(std::unique_ptr<uint8_t[]> data, uint32_t capacity)
        : data_(std::move(data)), capacity_(capacity), size_(0) {}

    uint8_t* MutableData() override { return data_.get(); }
    const uint8_t* Data() const override { return data_.get(); }
    uint32_t Size() const override { return size_; }
    uint32_t Capacity() const override { return capacity_; }

    bool SetSize(uint32_t size) override {
        if (size > Capacity()) {
            return false;
        }
        size_ = size;
        return true;
    }

private:
    std::unique_ptr<uint8_t[]> data_;
    uint32_t capacity_;
    uint32_t size_;
};

class ExternalMediaBufferImpl : public IMediaBuffer {
public:
    ExternalMediaBufferImpl(uint8_t* data, uint32_t capacity, uint32_t size,
                            MediaBufferReleaseCallback release, void* user)
        : data_(data),
          capacity_(capacity),
          size_(size),
          release_(release),
          user_(user) {}

    ~ExternalMediaBufferImpl() override {
        if (release_ != nullptr) {
            release_(data_, capacity_, user_);
        }
    }

    uint8_t* MutableData() override { return data_; }
    const uint8_t* Data() const override { return data_; }
    uint32_t Size() const override { return size_; }
    uint32_t Capacity() const override { return capacity_; }

    bool SetSize(uint32_t size) override {
        if (size > capacity_) {
            return false;
        }
        size_ = size;
        return true;
    }

private:
    uint8_t* data_ = nullptr;
    uint32_t capacity_ = 0;
    uint32_t size_ = 0;
    MediaBufferReleaseCallback release_ = nullptr;
    void* user_ = nullptr;
};

}  // namespace

std::shared_ptr<IMediaBuffer> CreateMediaBuffer(uint32_t capacity) {
    if (capacity == 0) {
        return nullptr;
    }
    std::unique_ptr<uint8_t[]> data(new (std::nothrow) uint8_t[capacity]);
    if (!data) {
        return nullptr;
    }
    return std::shared_ptr<IMediaBuffer>(
        new (std::nothrow) MediaBufferImpl(std::move(data), capacity));
}

std::shared_ptr<IMediaBuffer> CreateExternalMediaBuffer(
    uint8_t* data, uint32_t capacity, uint32_t size,
    MediaBufferReleaseCallback release, void* user) {
    if (data == nullptr || capacity == 0 || size > capacity) {
        return nullptr;
    }
    return std::shared_ptr<IMediaBuffer>(new (std::nothrow)
                                             ExternalMediaBufferImpl(
                                                 data, capacity, size, release,
                                                 user));
}

}  // namespace live_stream
