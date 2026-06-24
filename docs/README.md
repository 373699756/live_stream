# Documentation Index

本文档是 `docs/` 的总入口，只保留导航、标准优先级和最小读写规则。后续工程代码
重构、编码、评审和任务拆分以 `refactor/README.md` 为基准；模块契约写入拥有模块的
`libs/<module>.md`。

## 标准优先级

出现冲突时按以下顺序判断：

1. `refactor/README.md`：后续工程代码重构、编码、命名、边界、质量门禁和评审标准。
2. `libs/<module>.md`：模块 public API、配置、事件、HTTP API、状态来源和资源边界。
3. `web/web-console-design.md`：Web Console 页面、API 消费、认证上下文和 mock fallback。
4. `optimization/*.md`：热路径、内存、拷贝、fanout、板端采集和质量扫描专项。
5. 历史说明只用于追溯，不能作为新增代码或兼容层的依据。

## 文档入口

- `refactor/README.md`：重构计划总入口，也是后续工程重构和编码基准。
- `refactor/name.md`：命名收敛规则和当前通用泛名清理执行计划。
- `libs/README.md`：模块设计索引和统一模板。
- `libs/<module>.md`：每个实际 `libs/` 模块的长期设计文档。
- `libs/stream_hub_legacy.md`：历史迁移说明，不对应当前实际 `libs/` 模块。
- `web/web-console-design.md`：IPC/NVR 管理台设计与 API 消费说明。
- `optimization/memory.md`：热路径内存、拷贝、客户端 fanout 和质量扫描专项。
- `../scripts/README.md`：质量扫描、打包、板端热路径采集和 rootfs 模板脚本入口。

## 阅读规则

- 日常 bugfix：读 `AGENTS.md`、本文件、`refactor/README.md` 的开发工作流和相关模块文档。
- 跨模块重构：读 `refactor/README.md`、相关 `libs/<module>.md` 和相邻接口模块文档。
- HTTP/API/config/event 改动：读拥有模块文档，以及 `libs/http.md`、`libs/http_media.md`、
  `libs/event.md` 和 Web 文档；`libs/config.md`、`libs/alarm.md` 只用于追溯迁移历史。
- AI 改动：读 `libs/ai.md`、`libs/device.md`、`libs/hisi_vendor.md` 和 Web 文档。
- 质量扫描或热路径优化：读 `refactor/README.md` 的质量门禁和 `optimization/memory.md`。

## 落文档规则

- public C++ API、HTTP API、配置字段、事件 payload、AI 能力、升级状态机等长期契约，
  必须写入拥有模块的 `libs/<module>.md`。
- 跨模块命名、职责边界、重构阶段、开发流程和 DoD 更新到 `refactor/README.md`。
- Web 交互、页面状态和 API 消费方式更新到 `web/web-console-design.md`。
- 热路径资源模型、内存、拷贝、fanout 和板端采集更新到 `optimization/*.md`。
- 不再新增 active、app、architecture、contracts、features、quality 这类横切目录。
