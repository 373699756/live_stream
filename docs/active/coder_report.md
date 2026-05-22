# Coder Report

当前任务结果。只保留最近一次完成项或 blocker，替换旧内容，不追加历史。

## Task completed

继续 HTTP service 重构计划，完成 core 拆分和 executor 收敛。

## Problem fixed

`HttpServiceImpl` 之前同时承担业务装配、访问控制、TCP/HTTP core、连接状态、
executor 分流和 FLV 输出职责。现在 TCP/parse/connection/send/stream writer
已拆到私有 `HttpServer`，业务 handler 继续显式依赖真实服务。

## Files changed

主要变更在 `libs/http_service/` 和 `app/protocol_subsystem.cpp`。同时沿用前序
stream hub 窄接口改动。

## Behavior changed

HTTP API、URL、DTO、权限和错误码未主动变更。HTTP executor 从四组收敛为两组：
stream executor 处理 FLV/HLS/snapshot/WebRTC 实时链路，control executor 处理
普通 API、静态文件和控制/配置请求。

## Verification

通过：

- `make -C libs/http_service`
- `make -j2`
- `git diff --check -- libs/http_service app/protocol_subsystem.cpp ...`

未通过：

- `make -C libs/http_service test ROOT_DIR=/home/c/linux/hisi/live_stream`
  仍失败在旧测试接口：`infra::Status/Result`、旧 `IHttpService::Init/Deinit`、
  旧 dependency bag 等。按项目规则未主动迁移 `tests/`。

## Commit

Pending.

## Deviations

未引入第三方 HTTP 框架。没有迁移测试源码。

## Blocked or follow-up

HTTP tests 需要单独按当前接口迁移。
