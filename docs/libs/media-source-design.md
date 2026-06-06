# media_source Design

## 模块定位

`media_source` 拥有浏览器和协议共享的媒体源状态：HLS segment、FLV sequence/header
和 GOP cache、MJPEG 可用性、`MediaFrame`/`MediaTrack` 基础类型、时间戳修正和
播放 ready 字段。它不拥有 HTTP 请求解析、Web UI 状态、WebRTC peer 生命周期或
媒体配置应用。

## 总体框架图

```mermaid
flowchart LR
  Encoded[EncodedFrame from media_service] --> State[media_source stream state]
  State --> Stamp[TimestampCorrector]
  State --> GOP[GOP/FLV cache]
  State --> HLS[HLS playlist/segments]
  State --> MJPEG[MJPEG latest frames]
  State --> Status[browser status]
  HLS --> HTTP[http_service HLS]
  GOP --> FLV[http_service FLV]
  MJPEG --> MjpegApi[http_service MJPEG]
  Status --> StreamStatus[/api/status/streams]
```

## 核心职责

- 归一化下游协议时间戳，向 RTSP/WebRTC/HLS/FLV 提供单调相对 PTS/DTS。
- HLS playlist 只暴露已完成 segment，额外保留旧 segment 供短暂滞后客户端读取。
- 缓存 FLV sequence header 和关键帧 GOP，支持新客户端从可解码点开始。
- 维护 `browser_codec`、`hls_ready`、`flv_ready`、`mjpeg_ready`。
- 暴露 `IMediaSource`、`IMediaFlvSource`、`IMediaMjpegSource` 和
  `IMediaFrameSource` 的基础数据结构。

## 接口归属

public API 在 `media_source.h`、`media_frame.h`、`timestamp_corrector.h`。
`GET /api/status/streams` 的浏览器播放字段语义归本模块；HTTP 只序列化。

## 状态与资源模型

媒体源状态是高频路径共享状态。HLS segment body、FLV cached tag、EncodedFrame
引用需要明确 retain/unref。读取方拿到引用后必须遵循释放约定，不能直接引用内部
可变缓存。

## 风险与优化方向

- 时间戳回退、跳变或跨 codec 切换必须重建相关缓存。
- HLS 半成品 segment 不得进入 playlist。
- FLV/MJPEG 客户端数量应受限，避免内存和 socket 写压力失控。
