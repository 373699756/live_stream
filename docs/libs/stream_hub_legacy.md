# stream_hub_legacy

## 命名迁移

本模块命名迁移遵循仓库根目录 `重构.md` 的“任务 1 命名迁移基线”。后续目录、静态库、public header、接口类、Options/Dependencies/Stats、工厂函数和变量名只按该基线迁移；本文件中的旧 `_service`、`stream_*`、`MetaRtc*` 或 `Yang*` 名称仅表示迁移前名称、历史说明或明确允许保留的协议概念。HTTP REST 路径、配置 schema、Web DTO 和 ONVIF 返回路径可以随完全重构同步迁移；变更必须在同一任务内更新调用方、配置样例和文档，不保留旧兼容适配。

## 模块定位

`stream_hub_service` 是旧浏览器流 hub 命名下的过渡模块。生产主链路已经迁到
`media_source` 和 `media_pipeline`，旧兼容目录已删除。新设计和新代码不应继续
依赖 `stream_hub_service`。

## 历史职责

旧模块曾承载 HLS/FLV 浏览器流状态、时间戳归一化、缓存和 HTTP 直播 fanout。
这些职责现在拆分为：

- `media_source`：媒体源状态、HLS/FLV/MJPEG ready、GOP cache 和时间戳修正。
- `media_pipeline`：设备帧入口、媒体源装配和生命周期。
- `http` / `http_media`：HTTP 路由、媒体 HTTP 输出和 WebRTC signaling。

## 迁移规则

- 生产构建不再把 `stream_hub_service` 作为主链路依赖。
- 新 public API 使用 `Media*` 命名，不新增 `StreamHub*` 兼容别名。
- 旧名称只能出现在历史决策、迁移说明和删除前的兼容审计中。
- 顶层 `Makefile` 当前构建主链路使用 `media_pipeline`；`stream_hub_service`
  不应重新作为模块目录、库名或 public API 引入。

## 删除条件

- `app/`、`http`、`rtsp`、`webrtc` 生产代码不再 include `stream_hub_service.h`。
- 顶层构建、模块 `module.mk` 聚合和发布包不再依赖 `stream_hub_service`。
- 测试 fake 或历史测试完成迁移，或明确保留为 legacy test fixture。

## 状态与资源模型

该模块不再是当前媒体源状态拥有者。HLS/FLV/MJPEG ready、GOP cache、时间戳修正和
客户端注册上限都以 `media_source` / `media_pipeline` 文档为准。

## 风险与优化方向

- 文档或代码里若再次把 `stream_hub_service` 写成当前生产主线，会导致模块边界回退。
- 删除旧模块前需要确认无 app、HTTP、RTSP、WebRTC 生产依赖。
