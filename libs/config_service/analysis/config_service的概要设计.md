# config_service的概要设计

- 仓库根目录: `/home/cp/Public/hisi/live_stream/libs/config_service`
- 文档目标: 说明 config_service 在单进程多线程 IPC 工程中的主体作用、依赖上下文、总体设计和后续详细设计入口。

## 当前分析焦点

<!-- GENERATED:CURRENT-FOCUS START -->
- 分析范围: `libs/config_service`
- 当前阶段: source-corrected-design
- 当前主题: 按当前源码修正配置中心分析文档，移除旧版 `ConfigUpdateRequest`/schema 化设计结论
- 当前模块: config_service
- 当前链路: 配置树读写链路：路径校验 -> diverge 过滤 -> JSON 解析 -> store 候选树 -> verify/apply/notify -> 持久化或延迟保存 -> commit
- 当前文件: `include/config_service.h`, `src/config_service.cpp`, `src/config_store.cpp`, `src/config_persistence.cpp`, `src/config_signal.cpp`
- 当前函数: `ConfigServiceImpl::SetConfig`, `ConfigServiceImpl::GetConfig`, `ConfigPersistence::LoadOrCreate`, `ConfigStore::BuildSetConfig`
- 参考文档: `/home/cp/Public/hisi/live_stream/docs/ipc_software_design.md`
- 最近已读文件: `include/config_service.h`, `src/config_service.cpp`, `src/config_store.{h,cpp}`, `src/config_persistence.{h,cpp}`, `src/config_signal.{h,cpp}`, `src/config_name.cpp`, `tests/config_service_header_test.cpp`
- 主要风险: 当前实现没有字段级 schema、审计/event 适配、回调注销 token、fsync/CRC/双备份；`DelaySave` 和 `Stop()` 自动保存存在掉电或错误可见性风险
- 下一步: 业务 service 接入后补齐字段约束、权限/审计适配、event_service 通知适配和持久化可靠性
- 状态说明: 当前源码实现是配置树 API 和 callback registry，不是旧文档描述的 `UpdateGroupConfig` schema 版本事务模型
- 最近更新时间: 2026-04-26 11:58:47 +0800
<!-- GENERATED:CURRENT-FOCUS END -->

## 模块主体作用

- 主体职责: config_service 是 IPC 系统中的配置中心库，向其他 service 提供 JSON 配置树的读取、路径级写入、延迟保存、回调校验、运行态应用和通知注册能力。
- 解决的问题: 将配置文件读写、JSON 解析、配置路径寻址、回调分发和持久化提交集中到一个库内，减少 HTTP、ONVIF、media、network 等入口或业务模块直接操作配置文件。
- 不负责的范围: 当前源码不做用户权限校验、不生成审计记录、不直接发布 event_service 事件、不实现字段级 schema/范围校验，也不直接调用海思 SDK 或业务 service 私有实现。
- 当前源码权威结论: public API 是 `SetConfig/GetConfig/SaveFile/Attach*`；旧分析文档中的 `ConfigUpdateRequest`、`DefaultConfigSchemas`、`UpdateGroupConfig`、`IConfigChangeSink`、`IConfigAuditSink` 在当前源码中不存在。

## 在工程中的位置

- 所处层级: `libs/config_service` 是主程序 `live_stream` 的内部静态库，位于 `infra_service` 基础能力之上，为业务 service 和入口模块提供配置控制面能力。
- 上游依赖: Web/HTTP/ONVIF 或其他管理入口先完成请求解析和权限判断，再调用 `IConfigService::SetConfig` 或 `GetConfig`。
- 下游依赖: 运行态应用和通知通过 `ConfigProc` 注册到 apply/verify/notify map；配置转换通过 `ConfigFilterProc` 注册到 converge/diverge map。
- 与其它核心模块的关系: config_service 只依赖 infra 的 `Status`、`Result`、`IService`、`File`、`Path`、`Mutex`，不 include 其他业务 service 的私有头文件。

## 依赖上下文

- 编译依赖: public header 依赖 `infra_service/include`、C++ 标准库和 `3rdparty/nlohmann_json.hpp`；实现文件依赖本模块内 `config_store`、`config_persistence`、`config_signal`、`config_name`。
- 运行依赖: `ConfigServiceOptions::storage_path` 为空时只使用内存配置；非空时从 JSON 文件加载或用 `default_config_json` 创建文件。
- 配置输入: 默认配置来自 `ConfigServiceOptions::default_config_json`，必须是 JSON object；当前源码没有内置 16 个默认配置组清单。
- 硬件或板级依赖: 本库不直接依赖海思 MPP、MMZ、GPIO、寄存器、设备树、内核驱动或网络 socket。
- 外部文档依赖: `/home/cp/Public/hisi/live_stream/docs/ipc_software_design.md` 将 config_service 定位为统一配置入口；当前源码实现了统一入口和 JSON 文件持久化，但未实现文档中更完整的审计、事件和字段 schema 能力。

## 嵌入式特定约束

- 启动与初始化约束: 调用方创建 service 后先 `Init()` 加载或创建配置，再 `Start()` 允许写入；`GetConfig()` 要求 initialized，`SetConfig()` 要求 started。
- 存储或分区约束: 配置文件通常位于可写文件系统；保存时先写 `storage_path + ".tmp"`，再 rename 到正式路径，降低正式文件半写风险。
- 时序与掉电约束: `kConfigApplyDelaySave` 会只提交内存并标记 changed，后续由 `SaveFile()` 或 `Stop()` 尝试落盘；掉电前未保存会丢失延迟配置。
- 并发约束: 共享状态由 `infra::Mutex` 保护；外部回调在锁外执行，避免 callback 反向调用导致直接死锁。
- 控制面边界: 本库只处理配置控制面，不参与音视频帧、码流、网络会话或实时数据面。

## 总体架构

### 总体框架图

```mermaid
%%{init: {"themeVariables": {"fontSize": "16px"}, "flowchart": {"useMaxWidth": false, "htmlLabels": true, "nodeSpacing": 55, "rankSpacing": 95}}}%%
graph TD
    DOC["ipc_software_design.md<br/>统一配置入口约束"] --> API["include/config_service.h<br/>IConfigService"]
    ENTRY["HTTP/ONVIF/Web/业务入口<br/>SetConfig/GetConfig"] --> API
    INFRA["infra_service<br/>Status/Result/IService/File/Path/Mutex"] --> IMPL["ConfigServiceImpl"]
    API --> IMPL
    IMPL --> STORE["ConfigStore<br/>JSON root/version/changed"]
    IMPL --> PERSIST["ConfigPersistence<br/>LoadOrCreate/Save"]
    IMPL --> SIGNAL["ConfigSignalRegistry<br/>verify/apply/notify/converge/diverge"]
    IMPL --> JSON["nlohmann::json<br/>统一 JSON 值模型"]
    PERSIST --> FILE["storage_path JSON<br/>tmp + rename"]
    STORE --> SNAPSHOT["GetConfig(\"All\") 或路径配置"]
    SIGNAL --> CALLBACK["业务模块回调<br/>校验/应用/通知/转换"]
    classDef default font-size:16px;
```

图解说明:

- `IConfigService` 是唯一 public contract，隐藏配置存储、文件路径、锁和 JSON 解析细节。
- `ConfigServiceImpl` 只编排生命周期、锁边界和主链路；实际配置树、持久化和回调表分别由内部类承担。
- 外部业务集成通过 callback 完成，当前不直接绑定 event_service/logger_service，因此后续适配需要在上层或新增桥接层完成。

## 现有文档与源码关系

| 结论编号 | 文档路径 | 文档结论 | 当前源码状态 | 修正说明 |
| --- | --- | --- | --- | --- |
| D1 | `/home/cp/Public/hisi/live_stream/docs/ipc_software_design.md` | config_service 是唯一配置读写入口。 | 部分确认 | `IConfigService` 集中提供读写和保存接口，但工程层仍需约束其他模块不要直接读写配置文件。 |
| D2 | 同上 | 配置修改应校验、应用、持久化、发布事件、审计。 | 部分确认 | 当前 `SetConfig` 顺序为 diverge、JSON parse、verify、apply、notify、save、commit；没有审计和 event_service 事件对象。 |
| D3 | 同上 | 默认配置组包括 video/audio/image 等 16 个业务域。 | 未实现 | 当前默认 JSON 由 options 传入，源码没有 `DefaultConfigSchemas()` 或内置组清单。 |
| D4 | 同上 | public API 放在 include，service 实现 `infra::IService`。 | 确认 | `IConfigService` 继承 `infra::IService`，`CreateConfigService()` 返回 `std::unique_ptr<IConfigService>`。 |
| D5 | 同上 | 初期配置文件使用 JSON。 | 确认并源码扩展 | 当前实现使用 `nlohmann::json`/`ConfigJson`，不再维护 infra 自研 JSON parser/writer。 |

- 可直接沿用的设计结论: 统一配置入口、JSON 文件存储、public API/实现分离、基于 infra 返回错误码。
- 源码修正的结论: 当前实现以任意 JSON object 配置树为根，不以固定配置组 schema 为中心；配置名称支持 `stream.bitrate` 和数组下标路径。
- 仍未落地的文档要求: 字段级 schema、统一审计、ConfigChanged 事件发布、串行任务队列、持久化可靠性增强。

## 后续详细设计入口

- 关键链路 1: `Init()` 通过 `ConfigPersistence::LoadOrCreate()` 加载或创建 JSON 根对象，并交给 `ConfigStore::Load()`。
- 关键链路 2: `SetConfig()` 执行路径级配置更新，是校验、应用、通知、保存和提交的主链路。
- 关键链路 3: `GetConfig()` 读取 `All` 或路径配置，并通过 converge callback 做对外视图转换。
- 关键链路 4: `ConfigSignalRegistry` 管理五类 callback，决定业务模块如何接入配置中心。
