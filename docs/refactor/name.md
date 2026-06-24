# 命名收敛计划

本文记录当前命名重构的执行口径。若与历史扫描或旧计划冲突，以本文和用户最近确认
的规则为准；`docs/refactor/question.md` 只作为历史扫描输入，不作为新增代码依据。

## 总体原则

- 命名直指业务事实，不用泛化大词掩盖职责边界。
- 一次改一个命名主题，同步调用方、文件名、文档和构建目标，不保留旧名 wrapper、
  alias 或兼容 adapter。
- 保留已有且职责明确的 `Impl` / `impl_` PIMPL 命名，不改成 `State` / `state_`。
- 不把 `Result` 作为通用返回对象名；只有已经有明确业务语义的历史类型才单独评估。
- `Status`、`Info`、`Stats` 按语义分工，不做机械替换。

## `Bus` / `Engine` / `Core` / `Impl`

| 旧词 | 新规则 |
| --- | --- |
| `Bus` | 不作为通用模块对象名。事件模块使用 `Bus`；业务模块使用自己的领域名。协议规范概念可保留，例如 ONVIF `device_service`。 |
| `Engine` | 不作为通用执行器名。网络 IO 层用 `NetIo`；AI 推理后端用 `AiBackendRunner`；WebRTC native peer 层用 `WebrtcPeerHost`。 |
| `Core` | 不表示“核心”。app 组合层用 `FoundationSubsystem`；AI 主流程用 `AiTaskRunner`。 |
| `Impl` | 保留。顶层 `XxxImpl` 如果只是 PIMPL 或具体实现类可以继续使用；不要为改名而改成 `State`。 |

## 本轮执行清单

### app 组合层

- `CoreSubsystem` -> `FoundationSubsystem`
- `core_subsystem.*` -> `foundation_subsystem.*`
- `ProtocolStartupRefs::core` -> `ProtocolStartupRefs::foundation`

### net 模块

- `INetEngine` -> `INetIo`
- `NetEngineOptions` -> `NetIoOptions`
- `CreateNetEngine()` -> `CreateNetIo()`
- `NetEngineImpl` -> `NetIoImpl`
- `net_engine.*` -> `net_io.*`
- `net_engine_impl.*` -> `net_io_impl.*`
- 下游 HTTP、RTSP、WebRTC、ONVIF、app 依赖变量同步使用 `net_io` / `net_io_`。

### WebRTC 模块

- `IWebrtcEngine` -> `IWebrtcPeerHost`
- `NativeWebrtcEngine` -> `NativeWebrtcPeerHost`
- `CreateWebrtcEngine()` -> `CreateWebrtcPeerHost()`
- `webrtc_engine.*` -> `webrtc_peer_host.*`
- WebRTC 内部持有对象使用 `peer_host` / `peer_host_`。
- WebRTC peer-host 回调、注册表和状态来源使用 `PeerHost` 命名，不再用 `Engine`。

### AI 模块

- `AiCore` -> `AiTaskRunner`
- `ai_core.*` -> `ai_task_runner.*`
- `AiInferenceEngine` -> `AiBackendRunner`
- `ai_engine.*` -> `ai_backend_runner.*`
- host stub 后端和 NNIE 后端继续使用具体后端名，例如
  `HostStubAiBackendRunner`、`Hi3516Dv300NnieBackendRunner`。
- `CreateAiEngine()` -> `CreateAiBackendRunner()`，对应 host / NNIE 工厂同步命名。

## `Status` / `Info` / `Stats` / `Counters`

| 词 | 使用规则 |
| --- | --- |
| `Status` | 保留给执行状态、解析状态、错误码或状态机枚举，例如 `ConfigStatus`、`EventStatus`、`RawParseStatus`。 |
| `Info` | 用于对外查询快照、当前配置视图或展示型对象，例如 `SystemInfo`、`TimeInfo`、`AlarmInfo`。 |
| `Stats` | 用于聚合运行统计、计数和指标，例如 `WebrtcStats`、`EventStats`、`RtcpFeedbackStats`。 |
| `Counters` | 不作为 public 或 internal 类型名继续扩散；已有计数聚合改成 `Stats`。 |

本轮计划中的快照类重命名：

- `SystemStatus` / `GetSystemStatus()` -> `SystemInfo` / `GetSystemInfo()`
- `NetStatus` / `GetInterfaceStatus()` / `ReloadStatus()` ->
  `NetInterfaceInfo` / `GetInterfaceInfo()` / `ReloadInterfaceInfo()`
- `TimeStatus` / `GetTimeStatus()` -> `TimeInfo` / `GetTimeInfo()`
- `UpgradeStatus` / `GetStatus()` -> `UpgradeInfo` / `GetUpgradeInfo()`
- `AlarmStatus` / `GetAlarmStatus()` -> `AlarmInfo` / `GetAlarmInfo()`
- `AiTaskStatus` / `GetTaskStatuses()` -> `AiTaskInfo` / `GetTaskInfoList()`
- `EventCounts` / `GetCounts()` -> `EventStats` / `GetStats()`
- `RtcpFeedbackCounters` -> `RtcpFeedbackStats`

HTTP JSON 字段名不因 C++ 类型改名自动改变；除非明确修改 API 契约，否则保持现有
wire schema，避免前端和外部调用方无意义破坏。

## 验证方式

每个批次完成后至少运行对应模块构建：

- `make -C libs/net`
- `make -C libs/webrtc`
- `make -C libs/ai`
- `make -C libs/event`
- `make -C libs/system`
- `make -C libs/http`

全量收口后运行：

- `make -j2`
- `make host-test`
- `python3 scripts/scan/quality_scan.py quick --scope changed --baseline scripts/scan/quality_baseline.json`
