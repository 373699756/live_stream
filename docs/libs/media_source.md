# media_source

## 命名迁移

本模块命名迁移遵循仓库根目录 `重构.md` 的“任务 1 命名迁移基线”。后续目录、静态库、public header、接口类、Options/Dependencies/Stats、工厂函数和变量名只按该基线迁移；本文件中的旧 `_service`、`stream_*`、`MetaRtc*` 或 `Yang*` 名称仅表示迁移前名称、历史说明或明确允许保留的协议概念。HTTP REST 路径、配置 schema、Web DTO 和 ONVIF 返回路径可以随完全重构同步迁移；变更必须在同一任务内更新调用方、配置样例和文档，不保留旧兼容适配。

## 模块定位

`media_source` 拥有浏览器和协议共享的媒体源状态：`MediaFrameReader` live
reader、GOP cache、HLS segment、FLV sequence/header、MJPEG 可用性、
`MediaFrame`/`MediaTrack` 基础类型、时间戳修正和播放 ready 字段。它不拥有
HTTP 请求解析、Web UI 状态、WebRTC peer 生命周期或媒体配置应用。
HLS/TS 和 FLV tag 构造是本模块内部实现细节，跟随 cache、ready 状态和 codec
generation 一起重建。
HLS 当前 segment append 前会按 PES/TS 分包结果预估本帧输出字节并预留容量；
完成 segment 后记录带余量的下一段容量，减少 segment 中途扩容时对已写 TS body 的
整段复制。

## 总体框架图

```mermaid
flowchart LR
  Encoded[EncodedFrame from device_media] --> State[media_source stream state]
  State --> Stamp[TimestampCorrector]
  State --> Ring[MediaFrameReader ring]
  Ring --> GOP[video GOP cache]
  State --> HLS[HLS playlist/segments]
  State --> FLV[FLV sequence/header cache]
  State --> MJPEG[MJPEG latest frame]
  State --> Runtime[runtime snapshot]
  HLS --> HTTP[http HLS]
  Ring --> Protocols[RTSP/WebRTC/HTTP-FLV/MJPEG]
  FLV --> HttpFlv[http FLV]
  MJPEG --> MjpegApi[http MJPEG]
  Runtime --> MediaStreams[/api/media/streams]
```

## 核心职责

- 归一化下游协议时间戳，向 RTSP/WebRTC/HLS/FLV 提供单调相对 PTS/DTS。
- 暴露 `MediaFrameReader`：新 reader 可选择 keyframe-first，启动时可读取当前
  GOP，之后从 live queue 拉取 `MediaFrameReaderFrame`。
- HLS playlist 只暴露已完成 segment，额外保留旧 segment 供短暂滞后客户端读取。
- 缓存 FLV sequence header 和关键帧 GOP，支持新客户端从可解码点开始。
- 维护 runtime snapshot：track ready、codec、running、protocol support/ready、
  reader/client count、cached frames/bytes、HLS bytes、last DTS 和 last reset reason。
- 暴露 `IMediaSource`、`IMediaFlvSource`、`IMediaMjpegSource` 和
  `IMediaFrameSource` 的基础数据结构。

## 接口归属

public API 在 `media_source.h`、`media_frame.h`、`timestamp_corrector.h`。
`GET /api/media/streams` 和 `GET /api/media/streams/{stream}` 的媒体 runtime 字段
语义归本模块；HTTP 只按冻结 DTO 序列化。

第二阶段冻结的媒体核心契约：

| 接口/类型 | 语义 |
| --- | --- |
| `MediaFrame` | 协议热路径唯一帧对象，持有 `EncodedFrame` owner 引用，显式携带 stream id、codec、video track、keyframe、corrected DTS/PTS 和 duration。 |
| `MediaTrack` | video-only track，携带 stream id、codec、90000 clock rate、VPS/SPS/PPS、ready 状态和 codec generation。H.264/H.265 只有拿到参数集后 ready，MJPEG 只有拿到 latest frame 后 ready。 |
| `TimestampCorrector` | 每路 stream 独立维护 corrected DTS/PTS；按 ZLM `Stamp/DeltaStamp` 思路处理时间戳回退、超过 3s 的跳变、relative timestamp、stream stop、codec 切换和 reset，并把 rollback/jump 作为 reset 信号返回给 `media_pipeline`。 |
| `AttachFrameReader` | 创建 reader；`keyframe_first=true` 时 live 输出从下一个关键帧开始。 |
| `GetFrameReaderStartData` | 返回 reader 创建后可用的 stream running、track、当前 GOP 和 reader generation。 |
| `PopFrameReaderFrame` | 拉取 live frame；空队列返回 `false`，不会复制 payload，只增加底层 `VideoBuffer` 引用。 |
| `DetachFrameReader` | 释放 reader live queue，调用方必须先 unref 已取出的 frame。 |
| `GetFrameReaderStatus` | 返回 attached、pending frames、waiting keyframe、slow reader 和 close reason。 |

生产协议输出必须使用 corrected DTS/PTS。RTSP、WebRTC、HTTP-FLV、MJPEG 不再通过
旧 push sink 注册全局 fanout；每个 client/session/peer 必须显式持有
`MediaFrameReaderId` 并在关闭路径 detach。

`MediaStreamRuntime` 字段冻结为：

| 字段 | 语义归属 |
| --- | --- |
| `stream` | `main` 或 `sub` |
| `available` | 当前固件是否支持该码流 |
| `running` | 对应码流是否正在接收有效编码帧 |
| `codec` | 当前媒体源观察到的 codec |
| `codec_generation` | codec、stream start/stop 或 timestamp reset 后递增，用于诊断缓存代际 |
| `track_ready` | video track 是否可用于协议输出 |
| `hls_supported` / `hls_ready` | HLS 是否支持当前 codec、是否已有完整 playlist/segment |
| `http_flv_supported` / `http_flv_ready` | HTTP-FLV 是否支持当前 codec、是否已有 sequence header 或 GOP 起点 |
| `mjpeg_supported` / `mjpeg_ready` | MJPEG 是否支持当前 codec、是否已有可输出帧 |
| `webrtc_supported` / `webrtc_ready` | WebRTC 是否支持当前 codec、native 协议栈是否可创建 peer |
| `reader_count` / `client_count` | 当前 media reader 和 HTTP media client 数 |
| `cached_frames` / `cached_bytes` | GOP/live cache 诊断字段 |
| `hls_bytes` | 已保留 HLS segment body 字节数 |
| `last_dts` | 最新 corrected DTS |
| `last_reset_reason` | 最近一次 stream reset、codec switch 或 timestamp reset 原因 |

`/api/config/*` 的能力和配置字段不归本模块；这些字段归 `device_media` 或对应配置
模块。能力字段不是运行 ready 状态。

## 状态与资源模型

媒体源状态是高频路径共享状态。HLS segment body、FLV cached tag、EncodedFrame
引用需要明确 retain/unref。读取方拿到引用后必须遵循释放约定，不能直接引用内部
可变缓存。

缓存资源由 `media_pipeline` 注入的 options 限制：HLS segment duration、
playlist depth、segment retain count、FLV client 上限、MJPEG client 上限和 frame
reader/client 上限。`media_source` 内部必须在 codec 切换、时间戳重置或 stream 停止时重建
sequence header、GOP cache、HLS 当前 segment、MJPEG latest frame 和 ready 字段。
FLV GOP cache 只引用底层 `EncodedFrame`/`VideoBuffer`，只复制固定小头部；
payload 不做 GOP 级二次拷贝。

reader/GOP 资源模型：

| 资源 | 上限/回收 |
| --- | --- |
| GOP cache | 每路最多 128 帧，从关键帧开始重建；codec 切换、stream stop 和 cache overflow 清理。 |
| reader live queue | 每 reader 最多 32 帧；溢出时清空该 reader live queue，标记 slow reader，并等待下一个关键帧。 |
| reader count | 由 `MediaPipelineOptions` 限制，runtime snapshot 报告 active reader/client 数；`MediaSourceStats` 同时报告总 slow reader 和 main/sub slow reader。 |
| timestamp | `TimestampCorrector` 在每路 stream 内独立维护；stream stop、codec 切换时 reset。时间戳回退或大跳变会平滑为 last delta，并触发缓存/reader reset。 |
| last frame timestamp | `GetStats()` 报告主/子码流最新 corrected DTS，用于资源观测和慢读者排查。 |

## 非目标

- 不暴露内部可变缓存给 HTTP 或协议模块长期持有。
- 不拥有客户端 socket、peer/session 或 Web UI 状态。
- 不处理音频 track。

## 风险与优化方向

- 时间戳回退、跳变或跨 codec 切换必须重建相关缓存。
- HLS 半成品 segment 不得进入 playlist。
- FLV/MJPEG 客户端数量应受限，避免内存和 socket 写压力失控。
