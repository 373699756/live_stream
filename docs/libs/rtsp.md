# rtsp

## 命名迁移

本模块命名迁移遵循仓库根目录 `重构.md` 的“任务 1 命名迁移基线”。后续目录、静态库、public header、接口类、Options/Dependencies/Stats、工厂函数和变量名只按该基线迁移；本文件中的旧 `_service`、`stream_*`、`MetaRtc*` 或 `Yang*` 名称仅表示迁移前名称、历史说明或明确允许保留的协议概念。HTTP REST 路径、配置 schema、Web DTO 和 ONVIF 返回路径可以随完全重构同步迁移；变更必须在同一任务内更新调用方、配置样例和文档，不保留旧兼容适配。

## 模块定位

`rtsp` 负责 RTSP protocol、session、认证和通过 `IMediaFrameSource`
reader 拉取视频帧。它不拥有 WebRTC signaling、HTTP API 路由或 ONVIF
metadata。

## 总体框架图

```mermaid
flowchart LR
  Client[RTSP client] --> RTSP[rtsp]
  RTSP --> Net[net]
  RTSP --> Auth[auth]
  RTSP --> Events[event]
  RTSP --> Reader[MediaFrameReader]
  RTSP --> Mux[RtpPacketizer/RtspMuxer]
  RTSP --> Source[IMediaFrameSource/media_pipeline]
  Reader --> Source
  Mux --> Net
  Source --> Media[media_source]
```

## 核心职责

- 监听 RTSP 端口并管理 sessions。
- 处理 DESCRIBE/SETUP/PLAY/TEARDOWN 等 RTSP 控制。
- 通过认证服务保护 RTSP 访问。
- DESCRIBE 根据 `MediaTrack` 生成 SDP。
- PLAY 后为 session 创建 `MediaFrameReader`，先输出启动 GOP，再拉取 live
  frame 并通过 `media_mux::RtpPacketizer` 输出 RTP。
- SETUP 绑定 TCP interleaved 或 session 私有 UDP RTP/RTCP transport。

## 接口归属

public API 在 `rtsp.h`。RTSP URL 可被 ONVIF URI provider 使用，但 RTSP
内部 session 状态不归 ONVIF。`RtspStreamPath()` 和 `BuildRtspStreamUrl()` 是
跨模块唯一的 stream path/URL 契约，调用方只能配合 `IRtsp::LocalAddress()` 使用，
不得在其它模块复制 `/live/main`、`/live/sub` 或 RTSP 端口拼接规则。

## 状态与资源模型

RTSP session 拥有控制连接、RTP/RTCP 传输状态、认证上下文、
`MediaFrameReaderId`、RTP sequence/SSRC 和发送统计。PLAY 后必须通过
`AttachFrameReader(keyframe_first=true)` 进入 reader 模型：

- `GetFrameReaderStartData` 返回的当前 GOP 只作为启动待发送帧临时持有，
  发送后立即释放，不在 RTSP 内部维护私有 GOP cache。
- live frame 通过 `PopFrameReaderFrame` 拉取，RTSP 不再注册全局
  `AttachFrameSink` fanout。
- TCP interleaved 与控制连接绑定；UDP SETUP 为该 session 创建 RTP 和 RTCP
  socket，RTCP 包当前只做基础接收忽略，避免客户端 receiver report 影响连接。
- TEARDOWN、控制连接断开、SETUP 切换 transport 或服务停止时必须取消发送
  timer、detach reader，并关闭 session 私有 UDP socket。

RTP 分片统一使用 `media_mux::RtpPacketizer`。发送层只负责把
`RtpPacketView` 转成 TCP interleaved 或 UDP datagram；media payload slice
异步发送时由 `net` 的 `NetBufferOwner` 保留底层 `VideoBuffer` 引用。

## 非目标

- 不拥有 HLS/FLV/MJPEG/WebRTC 浏览器预览状态。
- 不直接访问 `device_media` 或 HiSilicon SDK。

## 风险与优化方向

- RTSP 客户端断开必须及时 detach reader，`media_source` 的 reader count 应回落。
- reader 溢出时会由 `media_source` 标记 slow reader 并等待下一个关键帧；
  RTSP 只上报丢帧/慢客户端自适应事件，不维护私有缓存。
- 关键帧请求由 `AttachFrameReader(keyframe_first=true)` 触发媒体链路，
  RTSP 不额外维护关键帧调试开关。
