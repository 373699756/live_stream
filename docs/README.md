# Documentation Index

本文档索引用来控制上下文成本：先读短文档，只有需要时再读长参考。

## 高频入口

- `../AGENTS.md`：项目规则、构建命令、编码约定、AI 上下文策略。
- `active/current_milestone.md`：当前阶段、基线和近期任务。
- `active/module_contracts.md`：短模块边界图。
- `active/decision_log.md`：固定架构/协作决策。
- `active/ai_task_template.md`：给 AI 的任务模板。
- `active/ai_handoff.md`、`active/coder_report.md`：明确 planner/coder 交接时使用。

## 按复用范围分类

通用、可迁移到其他嵌入式项目：

- `active/ai_task_template.md`：AI 任务模板。
- `active/ai_handoff.md`、`active/coder_report.md`：planner/coder 交接格式。
- `ai/lessons-learned.md` 中的 AI 协作、token 平衡、任务拆分经验。

本工程通用，但不是视频专用：

- `active/current_milestone.md`：当前阶段和执行规则。
- `active/decision_log.md`：架构和协作决策。
- `contracts/api-config.md` 中的配置/API 兼容规则。
- `contracts/event-payloads.md`：进程内事件契约。
- `ai/embedded-quality-tooling.md`：嵌入式代码质量、性能和设计扫描工具清单。

视频实时预览专用：

- `active/module_contracts.md`：live_stream 模块边界短图。
- `architecture/overview.md`：本工程运行架构。
- `architecture/module-boundaries.md`：服务边界和视频链路职责。
- `performance/memory-optimization.md`：视频热路径内存优化。
- `ai/codex-skill.md`：live_stream 专用 Codex skill 草案。
- `../www/README.md`：IPC Web Console 和直播 API。

## 架构文档

- `architecture/overview.md`：整体运行架构、启动顺序、子系统关系。
- `architecture/module-boundaries.md`：服务模块职责、依赖方向和禁止范围。

## 契约文档

- `contracts/api-config.md`：配置 scope、HTTP API、前后端 DTO 契约。
- `contracts/event-payloads.md`：进程内事件类型、payload 和命名规则。

## 性能文档

- `performance/memory-optimization.md`：视频热路径内存、拷贝和分配优化计划。

## AI 协作文档

- `ai/codex-skill.md`：本工程 Codex skill 草案。
- `ai/embedded-quality-tooling.md`：嵌入式工程扫描工具现状、缺口和安装建议。
- `ai/lessons-learned.md`：项目复盘、AI 协作经验和 token 平衡策略。

## 前端文档

- `../www/README.md`：Web Console 技术栈、开发命令和后端 API 列表。

## 读取原则

- 日常 bugfix：读 `AGENTS.md`、`active/*` 和相关代码。
- 跨模块改动：再读 `architecture/module-boundaries.md`。
- API/config 改动：再读 `contracts/api-config.md` 和 `www/README.md`。
- 事件改动：再读 `contracts/event-payloads.md`。
- 性能热路径优化：再读 `performance/memory-optimization.md`。
- AI 工作流或复盘：再读 `ai/*`。
