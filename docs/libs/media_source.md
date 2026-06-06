# media_source

## 命名迁移

本模块命名迁移遵循仓库根目录 `重构.md` 的“任务 1 命名迁移基线”。后续目录、静态库、public header、接口类、Options/Dependencies/Stats、工厂函数和变量名只按该基线迁移；本文件中的旧 `_service`、`stream_*`、`MetaRtc*` 或 `Yang*` 名称仅表示迁移前名称、历史说明或明确允许保留的协议概念。HTTP REST 路径、配置 schema、Web DTO 和 ONVIF 返回路径可以随完全重构同步迁移；变更必须在同一任务内更新调用方、配置样例和文档，不保留旧兼容适配。

## 模块定位

`media_source` 拥有浏览器和协议共享的媒体源状态：HLS segment、FLV sequence/header
和 GOP cache、MJPEG 可用性、`MediaFrame`/`MediaTrack` 基础类型、时间戳修正和
播放 ready 字段。它不拥有 HTTP 请求解析、Web UI 状态、WebRTC peer 生命周期或
媒体配置应用。

## 总体框架图

```mermaid
flowchart LR
  Encoded[EncodedFrame from device_media] --> State[media_source stream state]
  State --> Stamp[TimestampCorrector]
  State --> GOP[GOP/FLV cache]
  State --> HLS[HLS playlist/segments]
  State --> MJPEG[MJPEG latest frames]
  State --> Status[browser status]
  HLS --> HTTP[http HLS]
  GOP --> FLV[http FLV]
  MJPEG --> MjpegApi[http MJPEG]
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

浏览器播放状态字段：

| 字段 | 语义归属 |
| --- | --- |
| `running` | 对应码流是否正在接收有效编码帧 |
| `browser_codec` | 当前 codec 是否可进入浏览器预览链路 |
| `hls_supported` / `hls_ready` | HLS 是否支持当前 codec、是否已有可播放 playlist/segment |
| `flv_supported` / `flv_ready` | HTTP-FLV 是否支持当前 codec、是否已有 sequence header 或 GOP 起点 |
| `mjpeg_supported` / `mjpeg_ready` | MJPEG 是否支持当前 codec、是否已有可输出帧 |
| `codec` | 当前媒体源观察到的 codec |
| `hls_segment_count`、`*_size` | 诊断字段，只描述媒体源缓存状态 |

`/api/media/capabilities` 的 stream available/smart codec 等能力字段不归本模块；
这些字段归 `device_media`。能力字段不是运行 ready 状态。

## 状态与资源模型

媒体源状态是高频路径共享状态。HLS segment body、FLV cached tag、EncodedFrame
引用需要明确 retain/unref。读取方拿到引用后必须遵循释放约定，不能直接引用内部
可变缓存。

缓存资源由 `media_pipeline` 注入的 options 限制：HLS segment duration、
playlist depth、segment retain count、FLV client 上限、MJPEG client 上限和 frame
sink 上限。`media_source` 内部必须在 codec 切换、时间戳重置或 stream 停止时重建
sequence header、GOP cache、HLS 当前 segment 和 ready 字段。

## 非目标

- 不暴露内部可变缓存给 HTTP 或协议模块长期持有。
- 不拥有客户端 socket、peer/session 或 Web UI 状态。
- 不处理音频 track。

## 风险与优化方向

- 时间戳回退、跳变或跨 codec 切换必须重建相关缓存。
- HLS 半成品 segment 不得进入 playlist。
- FLV/MJPEG 客户端数量应受限，避免内存和 socket 写压力失控。
