#include "media/media_buffer.h"

#include <cstdlib>

namespace live_stream {

VideoBuffer* VideoBufferAlloc(uint32_t capacity) {
    if (capacity == 0) {
        return nullptr;
    }
    uint8_t* data = static_cast<uint8_t*>(std::malloc(capacity));
    if (data == nullptr) {
        return nullptr;
    }
    VideoBuffer* buffer =
        VideoBufferCreateExternal(data, capacity, 0, nullptr, nullptr);
    if (buffer == nullptr) {
        std::free(data);
    }
    return buffer;
}

VideoBuffer* VideoBufferCreateExternal(uint8_t* data, uint32_t capacity,
                                       uint32_t size,
                                       MediaBufferReleaseCallback release,
                                       void* user) {
    if (data == nullptr || capacity == 0 || size > capacity) {
        return nullptr;
    }
    VideoBuffer* buffer = static_cast<VideoBuffer*>(
        std::malloc(sizeof(VideoBuffer)));
    if (buffer == nullptr) {
        return nullptr;
    }
    buffer->data = data;
    buffer->capacity = capacity;
    buffer->size = size;
    buffer->ref_count = 1;
    buffer->release = release;
    buffer->user = user;
    return buffer;
}

VideoBuffer* VideoBufferRetain(VideoBuffer* buffer) {
    if (buffer == nullptr) {
        return nullptr;
    }
    (void)__sync_add_and_fetch(&buffer->ref_count, 1);
    return buffer;
}

bool VideoBufferSetSize(VideoBuffer* buffer, uint32_t size) {
    if (buffer == nullptr || size > buffer->capacity) {
        return false;
    }
    buffer->size = size;
    return true;
}

void VideoBufferRelease(VideoBuffer* buffer) {
    if (buffer == nullptr) {
        return;
    }
    if (__sync_sub_and_fetch(&buffer->ref_count, 1) != 0) {
        return;
    }
    if (buffer->release != nullptr) {
        buffer->release(buffer->data, buffer->capacity, buffer->user);
    } else {
        std::free(buffer->data);
    }
    std::free(buffer);
}

}  // namespace live_stream
