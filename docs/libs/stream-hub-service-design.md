# stream_hub_service Design

## 模块定位

`stream_hub_service` 是旧浏览器流 hub 命名下的过渡模块。生产主链路已经迁到
`media_source` 和 `media_source_service`，新设计和新代码不应继续依赖
`stream_hub_service`。

## 历史职责

旧模块曾承载 HLS/FLV 浏览器流状态、时间戳归一化、缓存和 HTTP 直播 fanout。
这些职责现在拆分为：

- `media_source`：媒体源状态、HLS/FLV/MJPEG ready、GOP cache 和时间戳修正。
- `media_source_service`：服务壳、帧订阅和下游客户端注册。
- `http_service`：HTTP 路由和流式响应。

## 迁移规则

- 生产构建不再把 `stream_hub_service` 作为主链路依赖。
- 新 public API 使用 `Media*` 命名，不新增 `StreamHub*` 兼容别名。
- 旧名称只能出现在历史决策、迁移说明和删除前的兼容审计中。
- 顶层 `Makefile` 当前构建主链路使用 `media_source_service`；`stream_hub_service`
  目录仍存在时，只能作为遗留源码和测试兼容对象看待。

## 删除条件

- `app/`、`http_service`、`rtsp_service`、`webrtc_service` 生产代码不再 include
  `stream_hub_service.h`。
- 顶层构建、模块 `module.mk` 聚合和发布包不再依赖 `stream_hub_service`。
- 测试 fake 或历史测试完成迁移，或明确保留为 legacy test fixture。

## 状态与资源模型

该模块不再是当前媒体源状态拥有者。HLS/FLV/MJPEG ready、GOP cache、时间戳修正和
客户端注册上限都以 `media_source` / `media_source_service` 文档为准。

## 风险与优化方向

- 文档或代码里若再次把 `stream_hub_service` 写成当前生产主线，会导致模块边界回退。
- 删除旧模块前需要确认无 app、HTTP、RTSP、WebRTC 生产依赖。
