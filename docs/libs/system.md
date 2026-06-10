# system

## 命名迁移

本模块命名迁移遵循仓库根目录 `重构.md` 的“任务 1 命名迁移基线”。后续目录、静态库、public header、接口类、Options/Dependencies/Stats、工厂函数和变量名只按该基线迁移；本文件中的旧 `_service`、`stream_*`、`MetaRtc*` 或 `Yang*` 名称仅表示迁移前名称、历史说明或明确允许保留的协议概念。HTTP REST 路径、配置 schema、Web DTO 和 ONVIF 返回路径可以随完全重构同步迁移；变更必须在同一任务内更新调用方、配置样例和文档，不保留旧兼容适配。

## 模块定位

`system` 是设备系统运维模块，物理拥有系统状态/动作、时间同步和升级三组能力。
它继续通过独立 public interface 暴露 `ISystem`、`ITime`、`IUpgrade`，避免把三类
业务揉成一个大接口。平台动作分别通过 `ISystemPlatform`、`ITimePlatform`、
`IUpgradePlatform` 注入，Linux/板端实现仍由 `app/platform/linux` 提供。

## 总体框架图

```mermaid
flowchart LR
  HTTP[http system handlers] --> System[system]
  HTTP --> Time[time handlers]
  HTTP --> Upgrade[upgrade handlers]
  System --> Platform[ISystemPlatform]
  Time --> TimePlatform[ITimePlatform]
  Upgrade --> UpgradePlatform[IUpgradePlatform]
  Platform --> Linux[/proc /sys reboot etc]
  TimePlatform --> Linux
  UpgradePlatform --> Linux
  System --> Logger[logger]
  System --> Event[event]
  Time --> Logger
  Time --> Event
  Upgrade --> Logger
  Upgrade --> Event
```

## 核心职责

- 查询设备状态、资源状态和基础系统信息。
- 处理 reboot、factory reset 等系统管理动作。
- 输出操作日志和系统状态事件。
- 加载和应用时间/NTP/浏览器登录校时配置，发布 `kTimeChanged`。
- 管理升级校验、准备、写入、等待重启、取消等状态，发布
  `kUpgradeProgressChanged`。
- 拥有 `upgrade_package` 包格式、manifest 和解包校验逻辑；真正写 flash
  仍通过 `IUpgradePlatform` 完成。

## 接口归属

public API 在 `system.h`、`time_api.h`、`upgrade.h` 和 `upgrade_package.h`。
HTTP `/api/system/*`、`/api/time/*`、`/api/upgrade/*` 路由归 `http`，
页面展示归 Web。

## 状态与资源模型

系统状态来自 `ISystemPlatform` 对 Linux/板端信息的即时查询。reboot、factory reset
等动作必须经权限校验和操作日志记录；模块不缓存媒体 pipeline 状态。

时间配置仍使用 `time` scope。`browser_sync_on_login` 控制 Web 登录后是否用浏览器
当前 Unix 毫秒时间同步设备时间，`manual_sync_allowed=false` 时浏览器校时也会被关闭。

升级包上传后先落入临时目录，校验完成前不得写 flash。升级状态必须足够支撑 Web 展示，
取消只对尚未进入不可中断写入阶段的流程生效。普通在线升级不写 `boot` 分区。

## 非目标

- 不直接写 MTD 或绕过 `IUpgradePlatform`。
- 不拥有网络接口管理、DNS 配置或协议监听生命周期。
- 不在 Web 侧推导系统状态。

## 风险与优化方向

- 系统动作必须做权限和审计。
- 查询路径应保持轻量，避免频繁读取阻塞文件影响 Web 状态刷新。
- 时间跳变会影响日志、认证过期和媒体时间戳展示，需要记录关键变更。
- 写 flash 前必须校验签名、manifest、分区白名单和包完整性。
