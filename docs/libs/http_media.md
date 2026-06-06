# http_media

## 模块定位

`http_media` 是 HTTP 媒体入口模块，负责 HLS、HTTP-FLV、MJPEG 和 WebRTC signaling
等与浏览器预览相关的 HTTP 媒体输出。普通控制 API、认证和静态资源仍归 `http`。

## 设计目标与非目标

- 通过 `media_source` reader 获取媒体帧，通过 `net` session 输出长连接数据。
- HTTP 完全重构时同步迁移 `www/` 的 API client、mock 数据、类型定义和页面调用。
- 不拥有设备编码入口、GOP cache、私有 socket 队列或 WebRTC ICE/DTLS/SRTP 状态。
- 不保留旧 REST route alias 或旧 DTO 适配层。

## 核心职责

- HLS playlist/segment HTTP 输出。
- HTTP-FLV 长连接输出。
- MJPEG 输出。
- WebRTC signaling HTTP DTO 转换和鉴权入口。
- 媒体长连接的慢客户端断开、reader detach 和资源回落验收。

## 依赖边界

- 依赖 `http` 的路由/认证边界。
- 依赖 `media_source` 的 `MediaFrameReader`、GOP、时间戳和 reader count。
- 依赖 `net` 的 session、send queue、buffer limit 和 close callback。
- 依赖 `webrtc` 的 native signaling/session public API，但不持有 transport 状态。
