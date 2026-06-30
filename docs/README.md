# Documentation Index

本文档是 `docs/` 的总入口，只保留导航、标准优先级和最小读写规则。项目定位、构建
和运行入口写在根目录 `../README.md`；AI 协作、编码纪律和执行经验写在根目录
`../AGENTS.md`；重构目标和阶段计划写在 `refactor/README.md`；模块契约写入拥有模块的
`libs/<module>.md`。

## 标准优先级

出现冲突时按以下顺序判断：

1. `../AGENTS.md`：AI 协作、编码纪律、执行经验和禁止事项。
2. `refactor/README.md`：重构目标、目标架构、阶段计划和验收计划。
3. `libs/<module>.md`：模块 public API、配置、事件、HTTP API、状态来源和资源边界。
4. `web/web-console-design.md`：Web Console 页面、API 消费、认证上下文和 mock fallback。
5. 历史说明只用于追溯，不能作为新增代码或兼容层的依据。

## 文档入口

- `../README.md`：项目定位、产品范围、构建、运行和文档入口。
- `../AGENTS.md`：AI 协作、编码约定、设计原则、执行经验和提交规则。
- `refactor/README.md`：唯一重构计划入口，合并历史重构、命名收敛、质量扫描、
  AI/VDS 验证和后续功能路线。
- `libs/README.md`：模块设计索引和统一模板。
- `libs/<module>.md`：每个实际 `libs/` 模块的长期设计文档。
- `libs/stream_hub_legacy.md`：历史迁移说明，不对应当前实际 `libs/` 模块。
- `web/web-console-design.md`：IPC/NVR 管理台设计与 API 消费说明。
- `../scripts/README.md`：质量扫描、打包、板端热路径采集和 rootfs 模板脚本入口。

## 阅读规则

- 日常 bugfix：读 `../AGENTS.md`、本文件和相关模块文档；只有涉及架构边界时再读
  `refactor/README.md`。
- 跨模块重构：读 `../AGENTS.md`、`refactor/README.md`、相关 `libs/<module>.md` 和相邻接口模块文档。
- HTTP/API/config/event 改动：读拥有模块文档，以及 `libs/http.md`、`libs/http_media.md`、
  `libs/event.md` 和 Web 文档；`libs/config.md`、`libs/alarm.md` 只用于追溯迁移历史。
- AI 改动：读 `libs/ai.md`、`libs/device.md`、`libs/hisi_vendor.md` 和 Web 文档。
- 质量扫描或热路径优化：读 `../AGENTS.md` 的执行经验和 `refactor/README.md` 的热路径
  资源和内存模型章节。

## 落文档规则

- public C++ API、HTTP API、配置字段、事件 payload、AI 能力、升级状态机等长期契约，
  必须写入拥有模块的 `libs/<module>.md`。
- 跨模块职责边界和重构阶段更新到 `refactor/README.md`。
- 编码纪律、执行经验、任务模板和 DoD 更新到根目录 `../AGENTS.md`。
- Web 交互、页面状态和 API 消费方式更新到 `web/web-console-design.md`。
- 热路径资源模型、内存、拷贝、fanout 和板端采集更新到 `refactor/README.md` 的热路径
  资源和内存模型章节。
- 不再新增 active、app、architecture、contracts、features、quality 这类横切目录。
