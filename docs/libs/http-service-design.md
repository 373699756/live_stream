# http_service Design

## 模块定位

`http_service` 是 Web Console 和浏览器预览的 HTTP 边界。它拥有 HTTP server、
request parsing、认证边界、路由分发、DTO 转换、静态 UI 文件服务以及 HLS/FLV/
MJPEG/snapshot/WebRTC signaling 的 HTTP 入口。

## 总体框架图

```mermaid
flowchart LR
  Browser[www/browser clients] --> HTTP[http_service]
  HTTP --> Auth[auth_service]
  HTTP --> Config[config_service]
  HTTP --> Media[media_service]
  HTTP --> Source[media_source_service]
  HTTP --> Snapshot[snapshot_service]
  HTTP --> AI[ai_service IAiView]
  HTTP --> Device[system/time/network/alarm/upgrade]
  HTTP --> Protocol[rtsp/onvif/webrtc]
  HTTP --> Static[www dist]
```

## 核心职责

- 处理 HTTP 连接、keep-alive、request timeout、body size 和静态文件。
- 执行认证和权限检查。
- 按业务域注册 handlers：auth、config、media、snapshot、stream、AI、system、
  network、time、upgrade、operations、WebRTC。
- 做 DTO 转换，不拥有业务状态。
- 为 HLS、HTTP-FLV、MJPEG 和 snapshot 提供流式响应。

## API 归属

HTTP 路由由本模块实现，但业务语义归拥有模块：

| API 组 | 业务归属 |
| --- | --- |
| `/api/auth/*` | `auth_service` |
| `/api/config/video`, `/api/config/image` | `media_service` |
| `/api/config/overlay` | `region_service` |
| `/api/config/network` | `network_service` |
| `/api/config/snapshot` | `snapshot_service` |
| `/api/config/ai`, `/api/ai/*` | `ai_service` |
| `/api/media/capabilities` | `media_service` |
| `/api/status/streams`, `/api/hls/*`, `/api/flv/*`, `/api/mjpeg/*` | `media_source` / `media_source_service` |
| `/api/snapshot/*` | `snapshot_service` |
| `/api/system/status` | `system_service` |
| `/api/upgrade/*` | `upgrade_service` |
| `/api/operations*` | `logger_service` |
| `/api/webrtc/*` | `webrtc_service` |

## 状态与资源模型

HTTP 是最宽依赖模块。宽依赖只允许停留在 HTTP 边界，不允许业务模块反向依赖 HTTP
或 Web。流式响应使用 stream/control executor，避免控制 API 被直播写阻塞。

## 风险与优化方向

- `HttpServiceDependencies` 较宽，后续可把 handlers 变成独立构造的 handler 类。
- 流式接口必须处理慢客户端和断连，不能持有媒体内部锁长时间写 socket。
- DTO 字段变更必须同步 Web API 类型和拥有模块文档。
