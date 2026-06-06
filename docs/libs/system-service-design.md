# system_service Design

## 模块定位

`system_service` 负责设备系统状态查询和系统管理动作。它通过 `ISystemPlatform`
访问 Linux/板端状态，不拥有 HTTP 响应格式、升级流程或媒体 pipeline。

## 总体框架图

```mermaid
flowchart LR
  HTTP[http_service system handlers] --> System[system_service]
  System --> Platform[ISystemPlatform]
  Platform --> Linux[/proc /sys reboot etc]
  System --> Logger[logger_service]
  System --> Event[event_service]
```

## 核心职责

- 查询设备状态、资源状态和基础系统信息。
- 处理 reboot、factory reset 等系统管理动作。
- 输出操作日志和系统状态事件。

## 接口归属

public API 在 `system_service.h`。HTTP `/api/system/status` 路由归
`http_service`，页面展示归 Web。

## 状态与资源模型

系统状态来自 `ISystemPlatform` 对 Linux/板端信息的即时查询。reboot、factory reset
等动作必须经权限校验和操作日志记录；模块不缓存媒体 pipeline 状态。

## 非目标

- 不拥有升级包校验、flash 写入或媒体启动停止流程。
- 不在 Web 侧推导系统状态。

## 风险与优化方向

- 系统动作必须做权限和审计。
- 查询路径应保持轻量，避免频繁读取阻塞文件影响 Web 状态刷新。
