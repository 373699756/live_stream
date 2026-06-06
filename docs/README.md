# Documentation Index

本文档目录按“总体框架 -> app 组合根 -> Web Console -> libs 模块 ->
运维交付”的方式组织。长期设计不再按 contracts/features 这类横切分类存放；
API、配置、事件、AI、升级、质量工具等内容都归入拥有它们的模块文档。

## Daily Entry

- `active/current_milestone.md`：当前阶段、基线和近期任务。
- `active/module_contracts.md`：短模块边界图。
- `active/decision_log.md`：固定架构和协作决策。

普通实现任务先读 `../AGENTS.md` 和 `active/*`。只有涉及架构、模块边界、
接口契约、Web、升级、质量工具或对应模块时，再读下面的长期设计文档。

## Architecture

- `architecture/system-design.md`：产品范围、总体分层、核心数据流和依赖方向。
- `architecture/runtime-composition-design.md`：Core/Device/Media/Protocol 子系统
  的组合、启动和关闭关系。
- `architecture/build-deploy-design.md`：构建、运行路径、配置文件、Web 静态资源、
  rootfs 和发布包关系。

## App

- `app/app-runtime-design.md`：进程入口、信号、路径解析、运行时配置、启动关闭顺序。
- `app/platform-adapters-design.md`：Linux system/time/network/upgrade 平台适配。
- `app/upgrade-runtime-design.md`：SPI NOR 分区、MTD 写入、升级包校验和
  `live_sysupgrade` 运行边界。

## Web

- `web/web-console-design.md`：IPC/NVR 管理台页面、导航、表单和 mock fallback。
- `web/api-consumption-design.md`：前端 API client、DTO、认证上下文和错误处理。
- `web/live-preview-design.md`：WebRTC/HLS/HTTP-FLV/MJPEG/snapshot 预览状态来源。

## Libs

- `libs/README.md`：模块设计索引和统一模板。
- `libs/*-design.md`：每个实际 `libs/` 模块的设计文档，包含
  `stream-hub-service-design.md` 这类 legacy 模块迁移说明。

所有 public C++ API、HTTP API、配置字段、事件 payload、AI 能力、升级状态机等
都必须归入拥有模块的 `libs/*-design.md` 或 `app/*-design.md`，不要再把实现设计
写成横切专题文档。

## Operations

- `operations/quality-tooling-design.md`：质量扫描工具、使用入口和报告归属。
- `operations/memory-optimization-design.md`：热路径内存和拷贝优化设计。
- `operations/release-package-design.md`：打包脚本、rootfs 脚本、发布包和板端部署。

## Read Rules

- 日常 bugfix：读 `AGENTS.md`、`active/*` 和相关代码。
- 跨模块改动：读 `architecture/system-design.md` 和相关 `libs/*-design.md`。
- HTTP/API/config/event 改动：读拥有模块文档、`libs/http-service-design.md`、
  `libs/config-service-design.md`、`libs/event-service-design.md` 和对应 Web 文档。
- AI 改动：读 `libs/ai-service-design.md`、`libs/media-service-design.md`、
  `libs/hisi-vendor-design.md`、`web/live-preview-design.md`。
- 升级/烧写/发布包改动：读 `app/upgrade-runtime-design.md`、
  `libs/upgrade-service-design.md`、`operations/release-package-design.md`。
- 质量扫描或热路径优化：读 `operations/quality-tooling-design.md` 和
  `operations/memory-optimization-design.md`。
