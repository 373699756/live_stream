#include "media/media_buffer.h"

#include <cstdlib>
#include <utility>

namespace live_stream {

struct MediaBuffer {
    uint8_t* data = nullptr;
    uint32_t capacity = 0;
    uint32_t size = 0;
    uint32_t refs = 0;
    MediaBufferFreeCallback free_callback = nullptr;
    void* user = nullptr;
};

namespace {

MediaBuffer* CreateMediaBuffer(uint8_t* data, uint32_t capacity, uint32_t size,
                               MediaBufferFreeCallback free_callback,
                               void* user) {
    if (data == nullptr || capacity == 0 || size > capacity) {
        return nullptr;
    }
    MediaBuffer* buffer =
        static_cast<MediaBuffer*>(std::malloc(sizeof(MediaBuffer)));
    if (buffer == nullptr) {
        return nullptr;
    }
    buffer->data = data;
    buffer->capacity = capacity;
    buffer->size = size;
    buffer->refs = 1;
    buffer->free_callback = free_callback;
    buffer->user = user;
    return buffer;
}

MediaBuffer* AddMediaBufferRef(MediaBuffer* buffer) {
    if (buffer == nullptr) {
        return nullptr;
    }
    (void)__sync_add_and_fetch(&buffer->refs, 1);
    return buffer;
}

void ReleaseMediaBuffer(MediaBuffer* buffer) {
    if (buffer == nullptr) {
        return;
    }
    if (__sync_sub_and_fetch(&buffer->refs, 1) != 0) {
        return;
    }
    if (buffer->free_callback != nullptr) {
        buffer->free_callback(buffer->data, buffer->capacity, buffer->user);
    } else {
        std::free(buffer->data);
    }
    std::free(buffer);
}

}  // namespace

MediaBufferRef::MediaBufferRef(const MediaBufferRef& other)
    : buffer_(AddMediaBufferRef(other.buffer_)),
      offset_(other.offset_),
      size_(other.size_) {}

MediaBufferRef& MediaBufferRef::operator=(const MediaBufferRef& other) {
    if (this == &other) {
        return *this;
    }
    MediaBuffer* retained = AddMediaBufferRef(other.buffer_);
    ReleaseMediaBuffer(buffer_);
    buffer_ = retained;
    offset_ = other.offset_;
    size_ = other.size_;
    return *this;
}

MediaBufferRef::MediaBufferRef(MediaBufferRef&& other) noexcept
    : buffer_(other.buffer_), offset_(other.offset_), size_(other.size_) {
    other.buffer_ = nullptr;
    other.offset_ = 0;
    other.size_ = 0;
}

MediaBufferRef& MediaBufferRef::operator=(MediaBufferRef&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    ReleaseMediaBuffer(buffer_);
    buffer_ = other.buffer_;
    offset_ = other.offset_;
    size_ = other.size_;
    other.buffer_ = nullptr;
    other.offset_ = 0;
    other.size_ = 0;
    return *this;
}

MediaBufferRef::~MediaBufferRef() {
    ReleaseMediaBuffer(buffer_);
}

MediaBufferRef MediaBufferRef::WrapExternalMemory(
    uint8_t* data, uint32_t capacity, uint32_t size,
    MediaBufferFreeCallback free_callback, void* user) {
    MediaBuffer* buffer =
        CreateMediaBuffer(data, capacity, size, free_callback, user);
    if (buffer == nullptr) {
        return MediaBufferRef();
    }
    return MediaBufferRef(buffer);
}

MediaBufferRef MediaBufferRef::Slice(uint32_t offset, uint32_t size) const {
    if (buffer_ == nullptr || offset > size_ || size > size_ - offset) {
        return MediaBufferRef();
    }
    return MediaBufferRef(AddMediaBufferRef(buffer_), offset_ + offset, size);
}

const uint8_t* MediaBufferRef::Data() const {
    if (!Valid()) {
        return nullptr;
    }
    return buffer_->data + offset_;
}

uint32_t MediaBufferRef::Size() const {
    return Valid() ? size_ : 0;
}

uint32_t MediaBufferRef::Capacity() const {
    if (buffer_ == nullptr || offset_ > buffer_->capacity) {
        return 0;
    }
    return buffer_->capacity - offset_;
}

bool MediaBufferRef::SetSize(uint32_t size) {
    if (buffer_ == nullptr || offset_ > buffer_->capacity ||
        size > buffer_->capacity - offset_) {
        return false;
    }
    const uint32_t required_size = offset_ + size;
    if (required_size > buffer_->size) {
        buffer_->size = required_size;
    }
    size_ = size;
    return true;
}

bool MediaBufferRef::Valid() const {
    if (buffer_ == nullptr || buffer_->data == nullptr) {
        return false;
    }
    if (offset_ > buffer_->size) {
        return false;
    }
    return size_ <= buffer_->size - offset_;
}

void MediaBufferRef::Reset() {
    ReleaseMediaBuffer(buffer_);
    buffer_ = nullptr;
    offset_ = 0;
    size_ = 0;
}

MediaBufferRef::MediaBufferRef(MediaBuffer* buffer)
    : buffer_(buffer), offset_(0), size_(buffer == nullptr ? 0 : buffer->size) {}

MediaBufferRef::MediaBufferRef(MediaBuffer* buffer, uint32_t offset,
                               uint32_t size)
    : buffer_(buffer), offset_(offset), size_(size) {}

MediaBufferRef::Builder MediaBufferRef::Builder::Allocate(uint32_t capacity) {
    if (capacity == 0) {
        return Builder();
    }
    uint8_t* data = static_cast<uint8_t*>(std::malloc(capacity));
    if (data == nullptr) {
        return Builder();
    }
    MediaBuffer* buffer = CreateMediaBuffer(data, capacity, 0, nullptr, nullptr);
    if (buffer == nullptr) {
        std::free(data);
        return Builder();
    }
    return Builder(MediaBufferRef(buffer));
}

MediaBufferRef::Builder MediaBufferRef::Builder::WrapExternalMemory(
    uint8_t* data, uint32_t capacity, uint32_t size,
    MediaBufferFreeCallback free_callback, void* user) {
    return Builder(MediaBufferRef::WrapExternalMemory(
        data, capacity, size, free_callback, user));
}

uint8_t* MediaBufferRef::Builder::Data() {
    if (!buffer_.Valid()) {
        return nullptr;
    }
    return buffer_.buffer_->data + buffer_.offset_;
}

const uint8_t* MediaBufferRef::Builder::Data() const {
    return buffer_.Data();
}

uint32_t MediaBufferRef::Builder::Size() const {
    return buffer_.Size();
}

uint32_t MediaBufferRef::Builder::Capacity() const {
    return buffer_.Capacity();
}

bool MediaBufferRef::Builder::Resize(uint32_t size) {
    return buffer_.SetSize(size);
}

bool MediaBufferRef::Builder::Valid() const {
    return buffer_.Valid();
}

MediaBufferRef MediaBufferRef::Builder::Finish() {
    MediaBufferRef finished = std::move(buffer_);
    buffer_.Reset();
    return finished;
}

void MediaBufferRef::Builder::Reset() {
    buffer_.Reset();
}

MediaBufferRef::Builder::Builder(MediaBufferRef buffer)
    : buffer_(std::move(buffer)) {}

}  // namespace live_stream
