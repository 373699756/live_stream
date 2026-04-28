/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: encoded_frame.h
 * Brief: 定义跨模块传递的编码媒体帧结构。
 */

#ifndef LIVE_STREAM_INFRA_ENCODED_FRAME_H_
#define LIVE_STREAM_INFRA_ENCODED_FRAME_H_

#include "infra/media_buffer.h"
#include "infra/stream_types.h"

#include <cstdint>
#include <memory>

namespace infra {

/**
 * @brief 编码媒体帧公共结构。
 *
 * 作用：
 * - 在 media_service 的 FrameSource 与 RTSP、WebRTC、Snapshot、Record 等 FrameSink 间传递。
 * - 用轻量元数据 + shared_ptr<IMediaBuffer> 引用表达一帧数据，避免复制 payload。
 *
 * payload 规则：
 * - buffer 指向整块底层内存。
 * - offset 表示本帧有效数据在 buffer 中的起始偏移。
 * - size 表示本帧有效数据长度。
 * - 调用方必须保证 buffer 非空且 offset + size <= buffer->Size()。
 *
 * 边界：
 * - 该结构只表达媒体帧，不包含 RTSP、WebRTC、ONVIF、HTTP、录像封装等协议字段。
 */
struct EncodedFrame {
    StreamId stream_id = StreamId::kMain;      ///< 码流标识，例如主码流、子码流或抓图码流。
    VideoCodec codec = VideoCodec::kH264;      ///< 编码格式。
    FrameType frame_type = FrameType::kP;      ///< 编码帧类型。
    FrameSequence sequence = 0;                ///< 单路码流内递增帧序号。
    int64_t pts_us = 0;                        ///< 显示时间戳，单位微秒。
    int64_t dts_us = 0;                        ///< 解码时间戳，单位微秒；未知时可等于 pts_us。
    std::shared_ptr<IMediaBuffer> buffer;      ///< payload 底层 buffer 引用。
    uint32_t offset = 0;                       ///< payload 在 buffer 中的起始偏移，单位 byte。
    uint32_t size = 0;                         ///< payload 有效长度，单位 byte。

    BufferSlice PayloadSlice() const {
        return BufferSlice{buffer, offset, size};
    }
};

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_ENCODED_FRAME_H_
