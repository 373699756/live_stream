# Web Live Preview Design

## 模块定位

Web 实时预览负责选择 WebRTC、HLS、HTTP-FLV、MJPEG 或 snapshot 路径，显示播放
状态和 AI 叠框。它不拥有媒体 pipeline，不计算 ready 状态，只消费后端 HTTP API。

## 总体框架图

```mermaid
flowchart LR
  LiveView[LiveViewPage] --> Metadata[usePreviewMetadata]
  LiveView --> Session[usePreviewPlaybackSession]
  Session --> Player[usePreviewPlayer]
  Player --> WebRTC[WebRTC signaling]
  Player --> HLS[/api/hls]
  Player --> FLV[/api/flv]
  Player --> MJPEG[/api/mjpeg]
  Player --> Snapshot[/api/snapshot]
  Metadata --> Status[/api/status/streams]
  LiveView --> Ai[useAiStatus / overlay]
```

## 预览模式

- WebRTC：低延迟预览，信令通过 `/api/webrtc/*`。
- HLS：浏览器兼容分段播放，通过 `/api/hls/{stream}/index.m3u8`。
- HTTP-FLV：连续直播，通过 `/api/flv/{stream}.flv`。
- MJPEG：multipart JPEG，通过 `/api/mjpeg/{stream}.mjpg`。
- snapshot：静态抓图，通过 `/api/snapshot/{stream}.jpg`。

## 状态规则

`GET /api/status/streams` 是播放状态权威来源。前端使用：

- `browserCodec`
- `hlsSupported` / `hlsReady`
- `flvSupported` / `flvReady`
- `mjpegSupported` / `mjpegReady`
- `webrtcReady`

能力字段来自 `GET /api/media/capabilities`，例如 stream 是否 available 或是否支持
smart codec。能力不是运行状态。

## AI 叠框

实时预览页轮询 `/api/ai/status`。只有检测结果来自当前码流且坐标有效时，Web 才把
`last_result.detections` 叠加到视频内容区域。AI 未启用、后端不可用或结果来自其他
码流时，只显示状态，不阻塞预览。

## 失败处理

播放器失败时只切换当前播放状态或允许用户选择其他模式，不反向修改后端配置。
后端 ready=false 时前端显示不可用，不自行请求关键帧或猜测编码器状态。
