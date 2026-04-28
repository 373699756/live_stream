/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: media_buffer.h
 * Brief: 定义跨模块共享媒体 payload buffer 接口。
 */

#ifndef LIVE_STREAM_INFRA_MEDIA_BUFFER_H_
#define LIVE_STREAM_INFRA_MEDIA_BUFFER_H_

#include <cstdint>
#include <memory>

namespace infra {

/**
 * @brief 媒体 payload buffer 接口。
 *
 * 作用：
 * - 承载编码后的音视频 payload 内存。
 * - 配合 EncodedFrame 的 buffer + offset + size 表达有效数据片段。
 * - 支持跨 RTSP、WebRTC、Snapshot、Record 等模块共享同一帧数据，减少中转拷贝。
 *
 * 所有权：
 * - 跨模块传递时使用 std::shared_ptr<IMediaBuffer> 管理生命周期。
 * - buffer 的具体来源可以是默认 heap buffer，也可以是 media_service 内部分档内存池。
 *
 * 边界：
 * - infra_service 只定义接口和默认 CreateMediaBuffer()。
 * - 海思 VB/MMZ、DMA、分档内存池等实现细节不能暴露到该 public header。
 */
class IMediaBuffer {
 public:
    virtual ~IMediaBuffer() = default;

    /**
     * @brief 获取可写内存起始地址。
     *
     * @return 返回可写字节指针；容量为 0 或实现无可写内存时可返回 nullptr。
     *
     * @note 调用方写入数据不能超过 Capacity()；跨模块共享后应避免继续修改内容。
     */
    virtual uint8_t* MutableData() = 0;

    /**
     * @brief 获取只读内存起始地址。
     *
     * @return 返回只读字节指针；容量为 0 或实现无内存时可返回 nullptr。
     */
    virtual const uint8_t* Data() const = 0;

    /**
     * @brief 获取当前有效数据长度。
     *
     * @return 返回有效字节数，单位 byte，必须小于等于 Capacity()。
     */
    virtual uint32_t Size() const = 0;

    /**
     * @brief 获取 buffer 总容量。
     *
     * @return 返回可存储的最大字节数，单位 byte。
     */
    virtual uint32_t Capacity() const = 0;

    /**
     * @brief 设置当前有效数据长度。
     *
     * @param size 有效字节数，调用方应保证 size <= Capacity()。
     *
     * @note 默认实现会把超过 Capacity() 的值截断到 Capacity()；调用方不应依赖该容错。
     */
    virtual void SetSize(uint32_t size) = 0;
};

struct BufferSlice {
    std::shared_ptr<IMediaBuffer> buffer;
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

class IMediaBufferPool {
 public:
    virtual ~IMediaBufferPool() = default;

    virtual std::shared_ptr<IMediaBuffer> Acquire() = 0;
    virtual MediaBufferPoolStats Stats() const = 0;
};

/**
 * @brief 创建默认 heap-backed 媒体 buffer。
 *
 * @param capacity buffer 容量，单位 byte，必须大于 0。
 *
 * @return 成功返回 shared_ptr<IMediaBuffer>；capacity 为 0 时返回空 shared_ptr。
 *
 * @note 该工厂用于基础测试和通用场景；媒体主链路后续应由 media_service 内存池创建 buffer。
 */
std::shared_ptr<IMediaBuffer> CreateMediaBuffer(uint32_t capacity);

std::shared_ptr<IMediaBufferPool> CreateFixedMediaBufferPool(
    uint32_t block_size, uint32_t block_count);

bool IsValidBufferSlice(const BufferSlice& slice);

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_MEDIA_BUFFER_H_
