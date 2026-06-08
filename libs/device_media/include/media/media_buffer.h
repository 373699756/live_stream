#ifndef LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_
#define LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_

#include <cstdint>
#include <memory>

namespace live_stream {

using VideoBufferFreeCallback = void (*)(uint8_t* data, uint32_t capacity,
                                         void* user);

// VideoBuffer 是编码后媒体 payload 的唯一内存 owner。跨模块保存或异步发送
// 只增加引用计数，不深拷贝 payload；最后一次 VideoBufferUnref() 才释放 malloc
// 内存或归还 pool block。
//
// data 可以来自 VideoBufferAlloc() 的堆内存，也可以来自
// VideoBufferCreateExternal() 包装的外部块。调用方只要把 data 指针保存到当前
// 调用栈之外，就必须先 VideoBufferRef()，并在不再使用时 VideoBufferUnref()。
struct VideoBuffer {
    uint8_t* data = nullptr;
    uint32_t capacity = 0;
    uint32_t size = 0;
    uint32_t ref_count = 0;
    VideoBufferFreeCallback free_callback = nullptr;
    void* user = nullptr;
};

struct BufferSlice {
    // slice 不持有引用，只描述 VideoBuffer 中的一段有效 payload。调用方需要
    // 另外持有 buffer 引用，才能把 slice 交给异步队列或协议 sender。
    const VideoBuffer* buffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct MediaBufferPoolStats {
    uint32_t block_size = 0;
    uint32_t block_count = 0;
    uint32_t free_count = 0;
    uint32_t in_use_count = 0;
    uint32_t high_water_count = 0;
    uint64_t no_memory_count = 0;
};

class IVideoBufferPool {
public:
    virtual ~IVideoBufferPool() = default;

    virtual VideoBuffer* Acquire() = 0;
    virtual MediaBufferPoolStats Stats() const = 0;
};

VideoBuffer* VideoBufferAlloc(uint32_t capacity);
VideoBuffer* VideoBufferCreateExternal(uint8_t* data, uint32_t capacity,
                                       uint32_t size,
                                       VideoBufferFreeCallback free_callback,
                                       void* user);
VideoBuffer* VideoBufferRef(VideoBuffer* buffer);
bool VideoBufferSetSize(VideoBuffer* buffer, uint32_t size);
void VideoBufferUnref(VideoBuffer* buffer);

std::unique_ptr<IVideoBufferPool> CreateVideoBufferPool(uint32_t block_size,
                                                        uint32_t block_count);

inline bool IsValidBufferSlice(const BufferSlice& slice) {
    if (slice.buffer == nullptr) {
        return false;
    }
    if (slice.offset > slice.buffer->size) {
        return false;
    }
    return slice.size <= slice.buffer->size - slice.offset;
}

inline const uint8_t *BufferSliceData(const BufferSlice& slice) {
    return IsValidBufferSlice(slice) ? slice.buffer->data + slice.offset
                                     : nullptr;
}

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_MEDIA_BUFFER_H_
