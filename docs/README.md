# Documentation Index

本文档目录按“README 总入口 -> libs 模块 -> Web Console -> optimization”的方式组织。
长期设计不再按 active/app/architecture/contracts/features 这类横切分类存放；API、
配置、事件、AI、升级、质量工具等内容都归入拥有它们的模块文档。

## Current Focus

稳定现有视频实时预览、抓图、配置和运维管理功能。优先保持后端主链路、HTTP API
和 Web Console 一致，不扩大到音频、录像、存储回放或无关 UI/API。

当前主链路是：

```text
app -> Core/Device/Media/Protocol
media_service -> media_source_service -> media_source
media_source -> RTSP/WebRTC/HLS/HTTP-FLV/MJPEG -> http_service -> www
```

## System Frame

`app/` 是组合根，负责路径解析、服务创建、依赖注入、启动顺序和关闭顺序。
启动顺序是 CoreServices、DeviceSubsystem、MediaSubsystem、ProtocolSubsystem；
停止顺序反向执行。Protocol 内部先启动 `net_service` 和 `media_source_service`，
再启动 RTSP、WebRTC、ONVIF 和 HTTP。

全局边界：

- `libs/*` 是业务和协议模块，模块通过 public interface、Options、
  Dependencies 或构造参数协作。
- `www/` 只消费 HTTP API 和 ready/status 字段。
- `scripts/`、`tools/`、`libs/upgrade-service-design.md` 和优化专项文档负责
  打包、rootfs 脚本、质量扫描和板端交付说明。
- 禁止 `libs/* -> app`、`libs/* -> www`、`www -> device SDK`。
- 产品不实现音频、录像、存储回放或相关 UI/API。

## Web

- `web/web-console-design.md`：IPC/NVR 管理台页面、API 消费、认证上下文、mock
  fallback、WebRTC/HLS/HTTP-FLV/MJPEG/snapshot 预览状态来源。

## Libs

- `libs/README.md`：模块设计索引和统一模板。
- `libs/*-design.md`：每个实际 `libs/` 模块的设计文档，包含
  `stream-hub-service-design.md` 这类 legacy 模块迁移说明。

所有 public C++ API、HTTP API、配置字段、事件 payload、AI 能力、升级状态机等
都必须归入拥有模块的 `libs/*-design.md`，不要再写成横切专题文档。

## Optimization

- `optimization/memory.md`：热路径内存、拷贝、客户端 fanout 和质量扫描专项。

## Read Rules

- 日常 bugfix：读 `AGENTS.md`、本文件和相关模块文档。
- 跨模块改动：读本文件和相关 `libs/*-design.md`。
- HTTP/API/config/event 改动：读拥有模块文档、`libs/http-service-design.md`、
  `libs/config-service-design.md`、`libs/event-service-design.md` 和对应 Web 文档。
- AI 改动：读 `libs/ai-service-design.md`、`libs/media-service-design.md`、
  `libs/hisi-vendor-design.md`、`web/web-console-design.md`。
- 升级/烧写/发布包改动：读 `libs/upgrade-service-design.md`。
- 质量扫描或热路径优化：读 `optimization/memory.md`。

## Work Rules

- 一轮 AI 任务通常只碰一个模块，最多再碰一个相邻接口模块。
- 写代码前先说明目标、范围、不做什么和验证方式。
- bugfix、rename、cleanup、refactor 不混在一个提交里。
- 新增接口、helper、class、hook 前先查已有接口；能直接写清楚就不要抽象。
- 配置和 HTTP API 变更必须同步后端 handler/DTO、`www/src/api/types.ts`、
  `www/README.md` 和拥有模块文档。
- 帧路径和高频路径不加普通日志；只保留错误、启动停止、配置变化和关键状态变化。
