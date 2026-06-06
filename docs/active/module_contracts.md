# Module Contracts

短模块契约图，用来避免每次任务重新发现模块边界。详细说明见
`docs/architecture/overview.md` 和 `docs/architecture/module-boundaries.md`。

## Cross-Module Rules

- `app/` 是组合根。服务之间通过窄接口、Options、Dependencies 或构造参数协作，
  不通过全局单例互相发现。
- HiSilicon MPP/VENC/ISP 细节留在 `media_service` 和 `hisi_vendor`
  的 SDK 边界内；区域叠加/遮挡由 `region_service` 通过 SDK 接口调用。
- 状态由最接近真实资源的模块拥有；上层只消费状态，不重复推导。
- 查询 API 返回具体业务类型；动作型 C++ 函数返回 `bool`。
- 不新增音频、录像、存储回放、录制 UI/API。
- 不新增只转调、只包装条件、只隐藏 2-3 行逻辑的 helper/class/hook。
- 帧路径和高频路径不加普通日志；只保留错误、启动停止、配置变化和关键状态变化。

## App

拥有进程入口、运行时路径、服务创建、依赖装配、启动顺序和关闭顺序。

不拥有具体业务逻辑、HTTP DTO 细节或前端行为。

## Core Services

`config_service` 拥有配置加载、默认值、validate/apply 回调。

`auth_service` 拥有登录、token、会话和认证校验。

`logger_service` 拥有操作日志，不拥有普通进程日志策略。

`event_service` 拥有进程内事件分发。事件命名和 payload 见
`docs/contracts/event-payloads.md`。

## Device Services

`system_service`、`time_service`、`network_service`、`alarm_service`、
`upgrade_service` 拥有设备管理能力和对应平台接口。

这些服务不拥有媒体 pipeline、协议路由或前端 DTO。

## Media

`media_service` 拥有：

- 视频 pipeline 生命周期。
- 主/子码流启动停止和状态。
- MPP/VENC/ISP SDK 适配。
- 编码帧输出、抓图来源、关键帧请求。
- 视频/图像配置应用。

不拥有 RTSP、HTTP、WebRTC、HLS/FLV 请求解析或 Web Console DTO。

## Region

`region_service` 拥有文字叠加和隐私遮挡配置应用、region 生命周期和
`hisisdk::IHisiSdk` region 接口调用。

不拥有 HiSilicon MPI/API 结构转换；这些实现在 `hisi_vendor` 内。

## Media Source

`media_source` 拥有：

- HLS/FLV 浏览器流封装状态。
- HLS segment 和 FLV sequence/keyframe 缓存。
- GOP cache、帧时间戳修正和浏览器协议共享媒体状态。
- `hls_ready`、`flv_ready`、`browser_codec` 等浏览器播放状态。

不拥有 HTTP 请求解析、Web UI 状态、WebRTC peer 生命周期或媒体配置。

`media_source_service` 是媒体源服务壳，负责从 `media_service` 接收编码帧、
维护下游 frame sink 和 HTTP-FLV/MJPEG 客户端注册；内部媒体状态使用
`media_source`。

## HTTP

`http_service` 拥有：

- HTTP 服务、认证边界、路由分发。
- API DTO 转换。
- 静态 UI 文件服务。
- HLS/FLV/snapshot/WebRTC signaling 的 HTTP 入口。

不拥有媒体状态、浏览器流 ready 计算或设备 SDK 调用。

## WebRTC

`webrtc_service` 拥有 WebRTC peer/session、SDP/ICE、候选发送和媒体传输集成。

WebRTC 是一种预览链路，不应污染 HLS/FLV 主链路状态。

## Frontend

`www/` 拥有 IPC/NVR 管理台 UI、页面状态、hooks、TypeScript DTO 和 mock fallback。

前端不解析 SDK 配置，不猜设备 ready 状态。直播可用性以 HTTP API 字段为准：
`browserCodec`、`hlsReady`、`flvReady`、`webrtcReady`。

## API/Config Contract

配置和 HTTP API 变更必须同步：

1. 后端 handler/DTO。
2. `www/src/api/types.ts` 和对应 API client。
3. `www/README.md`。
4. `docs/contracts/api-config.md`。
