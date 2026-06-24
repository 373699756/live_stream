# Libs Module Design Index

每个实际 `libs/` 模块都有一个对应设计文档。模块文档是长期设计正文入口；
HTTP API、配置 scope、事件 payload、AI、升级、质量优化等内容都要拆回拥有模块。

## Cross-Module Rules

- `app/` 是组合根。模块之间通过窄接口、Options、Dependencies 或构造参数协作，
  不通过全局单例互相发现。
- 状态由最接近真实资源的模块拥有；上层只消费状态，不重复推导。
- 查询 API 返回具体业务类型；动作型 C++ 函数返回 `bool`。
- 不新增音频、录像、存储回放、录制 UI/API。
- 不新增只转调、只包装条件、只隐藏 2-3 行逻辑的 helper/class/hook。
- HiSilicon MPP/VENC/ISP 细节留在 `device` 和 `hisi_vendor` 的 SDK 边界内。

## Naming Migration Baseline

命名迁移以 `docs/refactor/README.md` 的命名规则为准。模块文档、public
header、接口类、工厂函数、变量名和构建库名不得各自发明临时目标名。

- 目录和静态库使用业务域名，不再默认使用 `_service` 后缀。
- public header 使用目标模块名，例如 `http.h`、`rtsp.h`、`webrtc.h`、
  `media.h`。
- public interface 从 `I*Bus` 收敛到 `I*`；options/dependencies/stats 同步去掉
  多余 `Bus`。
- 工厂函数统一为 `Create<Module>()`；变量和 dependency 字段使用目标模块名。
- HTTP REST 路径、配置 JSON schema、Web DTO 可以按完全重构要求同步迁移。
- 旧 `stream_hub_service`、`stream_codec`、`stream_mux`、`MetaRtc*`、`Yang*`、
  `BackendName()` 和只转调旧接口的 wrapper 按 `docs/refactor/README.md` 的删除边界清理。
- ONVIF 规范里的 device/media service 概念可以保留 `Bus`，这不是模块后缀。

## Module Documents

### Foundation And Infrastructure

- `infra.md`
- `auth.md`
- `event.md`

### Device Modules

- `system.md`

### Media Modules

- `media.md`
- `device.md`
- `ai.md`
- `hisi_vendor.md`

### Protocol And Stream Modules

- `net.md`
- `http.md`
- `http_media.md`
- `rtsp.md`
- `webrtc.md`
- `onvif.md`
- `media_codec.md`

### Historical Migration Notes

- `alarm.md`
- `config.md`
- `logger.md`
- `net_stat.md`
- `rtp.md`
- `stream_hub_legacy.md`

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
