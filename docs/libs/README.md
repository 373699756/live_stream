# Libs Module Design Index

每个实际 `libs/` 模块都有一个对应设计文档。模块文档是长期设计正文入口；
HTTP API、配置 scope、事件 payload、AI、升级、质量优化等内容都要拆回拥有模块。

## Cross-Module Rules

- `app/` 是组合根。服务之间通过窄接口、Options、Dependencies 或构造参数协作，
  不通过全局单例互相发现。
- 状态由最接近真实资源的模块拥有；上层只消费状态，不重复推导。
- 查询 API 返回具体业务类型；动作型 C++ 函数返回 `bool`。
- 不新增音频、录像、存储回放、录制 UI/API。
- 不新增只转调、只包装条件、只隐藏 2-3 行逻辑的 helper/class/hook。
- HiSilicon MPP/VENC/ISP 细节留在 `media_service` 和 `hisi_vendor` 的 SDK 边界内。

## Module Documents

### Core And Infrastructure

- `infra-service-design.md`
- `config-service-design.md`
- `auth-service-design.md`
- `logger-service-design.md`
- `event-service-design.md`

### Device Services

- `system-service-design.md`
- `time-service-design.md`
- `network-service-design.md`
- `alarm-service-design.md`
- `upgrade-service-design.md`

### Media Services

- `media-service-design.md`
- `media-source-design.md`
- `media-source-service-design.md`
- `snapshot-service-design.md`
- `region-service-design.md`
- `ai-service-design.md`
- `hisi-vendor-design.md`

### Protocol And Stream Services

- `net-service-design.md`
- `http-service-design.md`
- `rtsp-service-design.md`
- `webrtc-service-design.md`
- `onvif-service-design.md`
- `stream-codec-design.md`
- `stream-mux-design.md`
- `stream-hub-service-design.md`

## Unified Template

模块文档优先保持以下章节。简单工具模块可以合并章节，但必须写清模块边界、
接口归属、状态/资源归属和非目标；不要只保留文件列表或泛化职责。

- `模块定位`
- `设计目标与非目标`
- `总体框架图`
- `核心职责`
- `关键流程`
- `接口归属`
- `状态与资源模型`
- `风险与优化方向`

当模块拥有 HTTP API、配置 scope、事件 payload、运行状态或热路径 buffer 时，
必须在拥有模块文档里写最小契约：字段归属、状态来源、生命周期、资源上限和失败边界。
