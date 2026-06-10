#include "media/media_buffer.h"

#include <cstdlib>

namespace live_stream {

FrameBuffer* FrameBufferAlloc(uint32_t capacity) {
    if (capacity == 0) {
        return nullptr;
    }
    uint8_t* data = static_cast<uint8_t*>(std::malloc(capacity));
    if (data == nullptr) {
        return nullptr;
    }
    FrameBuffer* buffer =
        FrameBufferCreateExternal(data, capacity, 0, nullptr, nullptr);
    if (buffer == nullptr) {
        std::free(data);
    }
    return buffer;
}

FrameBuffer* FrameBufferCreateExternal(uint8_t* data, uint32_t capacity,
                                       uint32_t size,
                                       FrameBufferFreeCallback free_callback,
                                       void* user) {
    if (data == nullptr || capacity == 0 || size > capacity) {
        return nullptr;
    }
    FrameBuffer* buffer = static_cast<FrameBuffer*>(
        std::malloc(sizeof(FrameBuffer)));
    if (buffer == nullptr) {
        return nullptr;
    }
    buffer->data = data;
    buffer->capacity = capacity;
    buffer->size = size;
    buffer->ref_count = 1;
    buffer->free_callback = free_callback;
    buffer->user = user;
    return buffer;
}

FrameBuffer* FrameBufferRef(FrameBuffer* buffer) {
    if (buffer == nullptr) {
        return nullptr;
    }
    (void)__sync_add_and_fetch(&buffer->ref_count, 1);
    return buffer;
}

bool FrameBufferSetSize(FrameBuffer* buffer, uint32_t size) {
    if (buffer == nullptr || size > buffer->capacity) {
        return false;
    }
    buffer->size = size;
    return true;
}

void FrameBufferUnref(FrameBuffer* buffer) {
    if (buffer == nullptr) {
        return;
    }
    if (__sync_sub_and_fetch(&buffer->ref_count, 1) != 0) {
        return;
    }
    if (buffer->free_callback != nullptr) {
        buffer->free_callback(buffer->data, buffer->capacity, buffer->user);
    } else {
        std::free(buffer->data);
    }
    std::free(buffer);
}

}  // namespace live_stream
