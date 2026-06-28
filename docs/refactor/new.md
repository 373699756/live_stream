# question.md 全量对比后的总体修复计划

## 摘要

本计划基于 `docs/refactor/question.md` 和当前代码的逐项对比整理。`question.md`
是历史扫描记录，不能照单全收，也不能只挑少数局部问题处理。当前 `media` 主链路已
经历一轮重构，旧 `media_service`、`stream_hub_service`、`EncodedFrame` 主路径、
手动 `MediaFlv*Unref` / `Subscription*Unref`、`media_streams.cpp` 单文件堆叠、
重复读锁方法、net send queue 无上限等问题已经基本解决。

剩余问题主要集中在：

- 当前未提交的大改需要先收口验证。
- HTTP handler 和依赖边界仍偏宽，存在两套 handler 创建 API。
- `media` 的资源预算和锁边界还不够显式。
- `hisi/device` SDK 接口过胖，hisi_vendor 内部锁和线程边界仍需梳理。
- app/subsystem 仍有全局单例、硬编码停止顺序和多套生命周期语义。
- 配置、metrics、回调契约和日志命名还有长期一致性问题。
- 注释、版权头、空目录、历史文档等风格债需要最后统一清理。

执行顺序为：先冻结当前基线，再修低风险实现债，然后按 HTTP、media、hisi/device、
app 生命周期、配置 metrics、风格文档分阶段推进。

## 当前状态对比

### 已处理，不再排修复

- 旧 `media_service` / `stream_hub_service` / `EncodedFrame` 主路径：当前已迁到
  `libs/media`、`MediaFrame`、`MediaBufferRef`。
- `MediaFlv*Unref`、`Subscription*Unref`、`MediaSegmentRefUnref`：当前 public API
  已是 RAII 值对象。
- `media_streams.cpp` 单文件堆叠：已拆出 `media_streams_impl`、
  `media_stream_state`、`FrameRing`、`PreviewClients`、`GopCache`、`HlsMaker`、
  `FlvMuxer`。
- `source_state/source_clients` namespace 别名：当前未发现残留。
- `IsHlsSupported`、`IsFlvSupported` 等重复读锁方法：当前已由 `ReadStream()`
  模板收敛。
- net send queue 无上限、closed connection 历史无上限：当前已有
  `send_queue_capacity`、`send_buffer_limit_bytes`、`pending_bytes_`、
  `kClosedConnectionInfoLimit`。
- 操作审计 logger 未重构：当前 `ILogger`、`CreateLogger()`、`OperationLogger`
  已是独立审计日志；不要把它和进程日志 `infra::Log` 混淆。

### 部分处理，还要继续

- RAII 已进入 public API，但 `MediaBuffer`、`PoolState` 内部仍用 `__sync_*`、
  `malloc/free`。
- `MediaStreams` 已拆真实状态对象，但仍是一把 `shared_mutex` 协调主/子码流、HLS、
  FLV、MJPEG、订阅。
- 局部资源上限已有，但缺统一 `MediaResourceBudget`，GOP bytes、live queue bytes、
  HLS segment bytes 不够显式。
- HTTP 控制面 handler 已删除 `HttpHandlerDependencies` 聚合包和
  `CreateHttpHandler(kind, deps)` 分发；`HttpImpl` 按业务入口直接构造每个 handler。
- HTTP router 和 handler 仍是静态 thunk + `void* user`。
- `HttpDependencies`、`SystemOverviewSources` 仍是宽依赖包。
- `FlvVideoTagBuild` / `MediaFlvVideoTagView` 仍含较大固定数组，需要减少栈上复制。
- 进程日志 `infra::Log` 仍用全局宏和文件内静态状态；这是 process log 的独立设计债，
  不是审计 logger 问题。

### 明确仍存在

- `libs/ai/src/ai.cpp` 仍有 `impl_ != nullptr` 防御式噪音。
- `IHisiSdk` 仍是大接口，混合 system、pipeline、venc、region、snapshot、image。
- hisi_vendor 仍有 `std::recursive_mutex` 和 `pthread_t`。
- `DefaultSdk()`、`MppSdk()`、`Application::Get()`、各 `Subsystem::Get()` 全局单例仍存在。
- `RequestKeyframeFn`、`MediaFrameCallback`、net/http 回调仍有 `void* user` C 风格接口。
- `app/application/application.cpp` 仍有 `volatile sig_atomic_t g_stop_requested`、
  SIGSEGV `_exit()`、硬编码 Stop 顺序。
- 多套生命周期状态仍并存：app subsystem 的 `started_`，device 的 `DeviceRunState`，
  system/time/network 的 `initialized_/started_`，media 的 `MediaStreamsRunState`，
  region/snapshot/rtsp/webrtc 各自状态。
- 配置校验仍分散在 device/system/protocol/ai 等模块，各自返回和错误上下文不统一。
- stats/metrics 仍分散为 `MediaStreamStats`、`NetStats`、`AiStats`、`EventStats`、
  各种 `Info` DTO。
- `logger.h` 命名仍容易误解为进程日志，实际是 operation audit。
- 注释语言、版权头、文件命名前后缀、空目录、根目录 `重构AI.md` 等风格债仍存在。

## 修复阶段

### P0：冻结当前基线

- 先处理当前工作树已有大量改动，不叠加新架构改动。
- 对已改的 HTTP、http_media、hisi_vendor、Web/API 相关文件按主题验证并提交。
- 更新 `question.md` 或新增状态表，标明每项为“已处理 / 部分处理 / 仍需修 /
  延后设计评审”。
- 验证命令：
  - `make -C libs/http`
  - `make -C libs/http_media`
  - `make -C libs/hisi_vendor`
  - `make -j2`
  - `python3 scripts/scan/quality_scan.py quick --scope changed --baseline scripts/scan/quality_baseline.json`

### P1：低风险实现债

- `ai.cpp`：删除 `impl_ != nullptr` 检查，构造成功后直接使用 `impl_`。
- `HttpServer::Prepare()` 等 prepare/start 路径：补每个失败点的错误日志，不先引入全局
  `Result<T>`。
- `MediaBuffer` / `MediaBufferPool`：把 `__sync_*` 改为 `std::atomic<uint32_t>`；
  保持 `MediaBufferRef` API 不变。
- `FlvVideoTagBuild`：禁止不必要拷贝或改为引用传递，避免大对象在热路径复制。
- 验证命令：
  - `make -C libs/ai`
  - `make -C libs/http`
  - `make -C libs/media`
  - `make host-test`

### P2：HTTP handler 与依赖边界

- 继续拆 `HttpDependencies`，按控制 handler、媒体 handler、streaming handler 三组在
  `HttpImpl` 构造期解包，避免 public 组合根 DTO 长期向实现层扩散。
- 将 `SystemOverviewSources` 收敛为 system status 专用只读视图，减少和
  `HttpDependencies` 重复。
- 在 `IHttpRouter` 增加成员函数注册模板，替换 handler 内静态 thunk + `void* user`。
- HTTP 对 RTSP/WebRTC/ONVIF 的直接依赖先收敛为只读诊断/会话视图接口，不改 HTTP JSON
  字段。
- 验证命令：
  - `make -C libs/http`
  - HTTP tests
  - Web 控制 API smoke test

### P3：media 资源预算和并发边界

- 新增 `MediaResourceBudget` 到 `MediaStreamsOptions`，覆盖 GOP frame/bytes、
  subscription queue frame/bytes、HLS segment count/bytes、FLV cached tag 上限。
- `FrameRing`、`GopCache`、`HlsMaker` 从硬编码常量迁到预算参数。
- 先做预算和统计，不立即引入 `FrameStage` pipeline。
- 评估锁拆分：主/子码流 `StreamTrack` 可分锁，`FrameRing` 和 `PreviewClients`
  保持独立状态对象。
- 验证场景：
  - 多客户端 HLS/FLV/RTSP/WebRTC 同时拉流。
  - 慢客户端触顶关闭。
  - 断开后 RSS、fd、client、subscription、cache 计数回落。

### P4：hisi/device 接口拆分

- 将 `IHisiSdk` 拆为窄接口：system、pipeline、venc stream、region、snapshot、image。
- `HardwarePipeline` 只依赖 system/pipeline/venc/image；`SnapshotCapture` 只依赖
  snapshot；`RegionOverlay` 只依赖 region。
- 同步 `StubHisiSdk` 和 `MppHisiSdk`，不保留旧大接口 wrapper。
- 消除 `std::recursive_mutex`：按系统、VI/VPSS/VENC、region、snapshot 操作梳理锁顺序。
- 评估 `pthread_t isp_thread_`：如果 SDK 必须用 pthread，封装成明确的 ISP thread 边界；
  否则迁到 `std::thread`。
- 验证命令和场景：
  - `make -C libs/device`
  - `make -C libs/hisi_vendor`
  - 抓图、OSD、隐私遮挡、图像配置、VENC stream 启停。

### P5：app 组合根和生命周期

- 去掉 `Application::Get()` 和 subsystem `Get()` 单例，改为 `Application` 持有 subsystem
  实例。
- 引入轻量 `SubsystemSlot` 或固定数组管理启动顺序，Stop 按反序执行，替代硬编码调用。
- 统一 app 层生命周期语义：Start 失败必须按已启动项反序回滚；Stop 可重复调用。
- 信号处理改为最小 async-signal-safe 标记；正常退出由主循环触发 Stop。
- SIGSEGV 路径保留最小诊断，避免承诺安全 flush C++ 日志。
- 验证场景：
  - 正常启动停止。
  - 启动中途失败。
  - 重复 Stop。
  - SIGINT/SIGTERM。

### P6：配置、metrics、回调契约

- 配置校验先统一错误结构和日志，不急于引入完整 JSON Schema。
- 各模块 `Verify/Apply` 保持归属模块内，但错误码、scope、message 统一。
- metrics 不替换现有 `Stats/Info` DTO；先新增内部聚合视图，HTTP 字段保持兼容。
- `RequestKeyframeFn`、`MediaFrameCallback` 后续改为窄接口或 callback 对象；嵌入式热路径
  不强行用 `std::function`。
- net/http 底层 `void* user` 回调可暂留在低层事件边界，但业务 handler 不再直接暴露
  thunk。
- 验证场景：
  - 配置热更新。
  - 错误响应。
  - Web status 页面。
  - media sessions 页面。

### P7：命名与风格清理

- `logger.h` 计划重命名为 `operation_log.h` 或 `operation_audit.h`，同步 include、docs
  和测试；不保留旧头 alias。
- 进程日志宏 `Info/Error/...` 后续改为 `INFRA_LOG_INFO` 这类前缀宏，或收敛为函数；
  这会牵动全工程，单独提交。
- 统一注释语言策略：模块文档和复杂业务注释可中文，public API 注释优先英文，或者按项目
  最终约定统一。
- 统一版权头策略：全有或全无，不继续半数文件带头。
- 清理空目录和根目录临时文档：`app/modules`、`libs/media/tests`、`重构AI.md` 等先确认
  用途，再删除或迁入 docs。
- 文件命名前后缀规则同步到 `docs/refactor/README.md`。
- 验证命令：
  - 旧名扫描。
  - include 扫描。
  - changed quality scan。

## 延后设计评审

- 完整 `FrameStage` pipeline：当前 media 已拆真实状态对象，直接抽 Stage 可能变成薄抽象。
- `libs/` 目录按层级重排：风险高、收益不直接，放到大版本边界。
- 全局 `Result<T, ErrCode>`：先用日志和局部错误结构补可诊断性，再决定是否引入。
- 全量 Metrics 框架：先从聚合视图开始，避免一次性改所有 HTTP/Web DTO。
- 配置 generation 无断连热切换：需要 device/media/protocol 联合设计，不能和资源预算混在
  同一批。

## 验证总表

- 基础：
  - `make -j2`
  - `make host-test`
- 模块：
  - `make -C libs/media`
  - `make -C libs/http`
  - `make -C libs/http_media`
  - `make -C libs/device`
  - `make -C libs/hisi_vendor`
  - `make -C libs/net`
- 质量：
  - `python3 scripts/scan/quality_scan.py quick --scope changed --baseline scripts/scan/quality_baseline.json`
- 协议：
  - HTTP-FLV
  - HLS
  - MJPEG
  - RTSP TCP/UDP
  - WebRTC 首帧和断连恢复
- 设备：
  - 抓图
  - OSD
  - 隐私遮挡
  - 图像配置
  - 关键帧请求
- 资源：
  - 慢客户端断开
  - 频繁连接断开
  - RSS、fd、client、subscription、cache 回落
- API/Web：
  - `/api/media/streams`
  - `/api/media/sessions`
  - `/api/system/overview`
  - `/api/ai/status`
  - `/api/alarm/status`

## 假设

- `question.md` 是历史扫描，不高于 `docs/refactor/README.md` 和模块文档。
- 当前未提交改动是有效工作，应先验证收口，不回滚。
- 每个阶段独立提交；不把 bugfix、rename、架构拆分和风格清理混在一个提交里。
- 不新增兼容 wrapper；接口收敛时同步调用方、文档、Web/mock 和质量扫描基线。
