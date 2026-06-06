# time_service Design

## 模块定位

`time_service` 负责系统时间、时区/NTP 配置和时间应用。它通过 `ITimePlatform`
调用平台能力，不拥有网络接口管理或浏览器时间格式化。

## 总体框架图

```mermaid
flowchart LR
  HTTP[http_service time/config handlers] --> Time[time_service]
  Time --> Config[config_service]
  Time --> Platform[ITimePlatform]
  Time --> Event[event_service]
  Time --> Logger[logger_service]
```

## 核心职责

- 加载和应用时间/NTP 配置。
- 提供时间状态查询。
- 发布 `kTimeChanged` 并记录运维操作。

## 接口归属

public API 在 `time_service.h`。前端只消费 HTTP 返回值，不负责设备时间应用策略。

## 风险与优化方向

- 时间跳变会影响日志、HLS segment 时间和认证过期判断，需要记录关键变更。
- NTP 配置失败应返回明确错误，不应静默写入不可用配置。
