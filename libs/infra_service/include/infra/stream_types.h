/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: stream_types.h
 * Brief: 定义媒体码流跨模块共享的基础枚举和值类型。
 */

#ifndef LIVE_STREAM_INFRA_STREAM_TYPES_H_
#define LIVE_STREAM_INFRA_STREAM_TYPES_H_

#include <cstdint>

namespace infra {

/**
 * @brief 码流标识。
 *
 * 作用：
 * - 标识主码流、子码流、抓图码流等公共媒体通道。
 * - 用于 media_service、RTSP、WebRTC、Snapshot 等模块之间传递码流选择。
 */
enum class StreamId {
    kMain = 0,   ///< 主码流，通常为高分辨率视频流。
    kSub,        ///< 子码流，通常为低分辨率或低码率视频流。
    kSnapshot,   ///< 抓图码流或 JPEG 输出通道。
};

/**
 * @brief 编码视频格式。
 *
 * @note 该枚举只表达编码格式，不表达 RTP payload type、SDP、WebRTC codec id 等协议字段。
 */
enum class VideoCodec {
    kH264 = 0,  ///< H.264/AVC 编码。
    kH265,      ///< H.265/HEVC 编码。
    kMjpeg,     ///< MJPEG/JPEG 编码。
};

/**
 * @brief 编码帧类型。
 *
 * @note 只描述媒体帧属性，不包含协议分包、RTP marker、WebRTC keyframe 等字段。
 */
enum class FrameType {
    kIdr = 0,  ///< IDR 关键帧。
    kI,        ///< I 帧。
    kP,        ///< P 帧。
    kB,        ///< B 帧。
    kJpeg,     ///< JPEG 单帧。
};

/**
 * @brief 单路码流内单调递增的编码帧序号。
 *
 * @note 序号由媒体生产侧维护，用于丢帧统计、调试和顺序判断，不等同于 RTP sequence。
 */
using FrameSequence = uint64_t;

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_STREAM_TYPES_H_
