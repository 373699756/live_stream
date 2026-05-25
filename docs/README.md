# Documentation Index

文档按“日常入口少、长期参考分类清楚”的原则组织。普通实现任务先读
`../AGENTS.md` 和 `active/` 下的短文档；长文档只在任务确实相关时读取。

## Daily Entry

- `active/current_milestone.md`：当前阶段、基线和近期任务。
- `active/module_contracts.md`：短模块边界图。
- `active/decision_log.md`：固定架构和协作决策。

## Architecture

- `architecture/overview.md`：整体运行架构、启动顺序、子系统关系。
- `architecture/module-boundaries.md`：服务职责、依赖方向和禁止范围。

## Contracts

- `contracts/api-config.md`：配置 scope、HTTP API、前后端 DTO 契约。
- `contracts/event-payloads.md`：进程内事件类型、payload 和命名规则。
- `../www/README.md`：Web Console 技术栈、开发命令和后端 API 列表。

## Features

- `features/ai.md`：AI/NNIE/IVE 功能状态、边界和板端验证顺序。
- `features/spi_nor_upgrade.md`：32M SPI NOR 分区、烧写、启动和升级包方案。

## Quality

- `quality/tooling.md`：嵌入式质量扫描工具、安装建议和使用方式。
- `quality/quality_report.md`：`scripts/quality_scan.sh` 生成的汇总报告。
- `quality/reports/<timestamp>/`：质量扫描原始日志目录，默认不纳入 git。
- `quality/memory_optimization.md`：视频热路径内存和拷贝优化计划。

## Read Rules

- 日常 bugfix：读 `AGENTS.md`、`active/*` 和相关代码。
- 跨模块改动：再读 `architecture/module-boundaries.md`。
- API/config 改动：再读 `contracts/api-config.md` 和 `../www/README.md`。
- 事件改动：再读 `contracts/event-payloads.md`。
- AI、升级或质量扫描任务：读对应 `features/*` 或 `quality/*`。
