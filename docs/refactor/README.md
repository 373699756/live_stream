# live_stream 重构计划

本文是 `docs/refactor/` 下唯一有效的重构计划。历史重构、命名扫描、质量扫描、AI/VDS 验证和后续功能路线已经合并到本文；编码细则和协作规则已提炼到根目录 `AGENTS.md`，项目定位和运行方式已提炼到根目录 `README.md`。

## 1. 重构目标

目标是把当前 IPC video-only 产品收敛成边界清晰、资源可控、可诊断、可回滚的板端服务。

核心目标：

- 设备采集和协议分发彻底分离。
- 媒体热路径有明确资源预算、背压和关闭路径。
- HTTP/Web API 表达产品能力，不暴露内部实现结构。
- AI 是可选能力，失败不影响直播主链路。
- app 组合根显式装配依赖，启动失败可反序回滚。
- 历史旧名、旧 wrapper、旧路径逐步删除，不保留长期兼容层。

非目标：

- 不引入音频、录像、回放、推流、代理、TURN/SFU、云信令或通用媒体服务器控制台。
- 不嵌入或修改 ZLMediaKit；仅参考协议行为和资源模型。
- 不为了“架构好看”做 `libs/` 目录大重排、全量 `Result<T>`、全量 Metrics 框架或抽象 Stage pipeline。

## 2. 目标架构

| 模块 | 应拥有的职责 | 不应拥有的职责 |
| --- | --- | --- |
| `app` | 组合设备、媒体、协议、HTTP、AI、告警、升级等运行期对象 | 媒体缓存、协议组包、硬件细节 |
| `libs/device` | 设备媒体链路、图像、区域、抓图、关键帧请求、硬件配置业务对象 | 协议输出、HTTP streaming、reader fanout |
| `libs/hisi_vendor` | HiSilicon MPP/VENC/ISP/REGION/SNAPSHOT/IVE/VGS/NNIE SDK 封装 | 业务状态、HTTP API、媒体订阅策略 |
| `libs/media` | 通用媒体帧、GOP/起播缓存、订阅、HLS/FLV/MJPEG 缓存、统计 | 设备配置、socket、HTTP request 解析 |
| `libs/media_codec` | H.264/H.265 AnnexB、AVCC/HVCC、RTP packet view、时间戳、payload 切片 | RTP session、协议连接生命周期、WebRTC transport |
| `libs/net` | EventLoop、Timer、TcpServer、TcpConnection、TcpClient、SocketUtil、send queue、背压观测 | HTTP、鉴权、媒体业务、配置逻辑 |
| `libs/http` | 控制 API、鉴权、短响应、统一错误 envelope、系统/配置/状态 API | HLS/FLV/MJPEG 长连接输出 |
| `libs/http_media` | HLS、HTTP-FLV、MJPEG、WebRTC signaling/WHEP | 控制 API、设备 SDK 配置、socket 队列管理 |
| `libs/rtsp` | RTSP session、SDP、SETUP/PLAY、RTP over TCP/UDP、RTCP | HTTP API、设备配置、媒体帧缓存所有权 |
| `libs/webrtc` | SDP、ICE、DTLS、SRTP、RTP sender、peer/session 生命周期 | 云信令、公网 relay、datachannel |
| `libs/onvif` | ONVIF 发现、设备服务、返回 RTSP URL | 媒体缓存、RTSP 传输实现 |
| `libs/ai` | AI 配置、抓帧调度、推理后端、告警输出、AI 状态和告警图片 | 媒体协议、设备 pipeline 生命周期、Web DTO |
| `www` | IPC/NVR 管理台、配置表单、实时预览、告警和运维页面 | 通用媒体服务器控制台、设备 SDK 配置解析 |

目标数据流：

```text
HiSilicon MPP -> device -> media -> http_media / rtsp / webrtc
                           -> http control API stats
AI抓帧 -> ai backend -> alarm / alert images / api status
```

关键依赖规则：

- `device` 拥有硬件生命周期，`media` 只消费编码帧。
- 协议模块只通过 `media` 获取帧和缓存，不直接访问设备 SDK。
- `http` 聚合控制和只读状态，但不能绕过拥有模块修改内部状态。
- AI 每次抓帧从 `DeviceMedia` 查询当前 channel；设备未 started 或正在重建时跳过。
- Web 状态来自后端明确字段，不重新推导设备 SDK 状态。
- `*Dependencies` DTO 已从 app/libs 生产代码删除：基础服务通过 `Runtime`，
  直播源通过 `MediaSourceRegistry`，协议只读状态通过 `ServiceRegistry`。registry 只能暴露
  基础服务、媒体订阅边界或只读诊断视图，不允许变成跨模块业务控制入口。

## 3. 当前基线

已完成或基本完成：

- 旧 `media_service`、`stream_hub_service` 和 `EncodedFrame` 主路径已迁到 `libs/media`、`MediaFrame`、`MediaBufferRef`。
- FLV start、subscription frame、HLS segment ref 等主路径已由 RAII 值对象管理。
- `MediaStreams` 已拆出 stream state、frame ring、preview clients、GOP cache、HLS maker、FLV muxer 等职责对象。
- net send queue、send buffer limit、pending bytes、closed connection 限制已有基础。
- HTTP handler 创建已从旧 `CreateHttpHandler(kind, deps)` 分发改为按业务入口构造。
- HTTP public `HttpDependencies` 已删除，基础服务、协议只读诊断和媒体源分别从
  `Runtime`、`ServiceRegistry`、`MediaSourceRegistry` 获取。
- HTTP system overview 不再保存内部只读聚合包，基础状态现场从
  `Runtime`、`ServiceRegistry`、`MediaSourceRegistry` 读取。
- `hisi_vendor` 已拥有 SDK 契约和生产实现入口，`device` 只消费窄接口。
- Web 预览 URL、preview readiness、AI badge、HTTP 媒体 helper 已完成多轮命名收敛。
- Debug 打包不再复制配置文件；release 配置默认从 `/config/*.json` 获取。

仍需处理：

- `net` callback 仍使用 `void* user` C 风格边界；HTTP router/handler 已使用
  `std::function` 注册，是否调整 net callback 契约需单独评估。
- `media` 资源预算和锁边界还不够显式。
- `app` 仍有全局单例式入口和硬编码生命周期。
- 配置校验错误结构、metrics 聚合视图、回调契约仍需统一。
- process log 与 operation audit 命名、注释语言、版权头、空目录用途等风格债待最后收口。

延后设计评审：

- 完整 `FrameStage` pipeline。
- `libs/` 按层级大重排。
- 全局 `Result<T, ErrCode>`。
- 全量 Metrics 框架。
- 配置 generation 无断连热切换。

## 4. 参考项目经验同步

本节只记录可迁移到 `live_stream` 的工程经验和教训，不作为引入旧代码、旧接口、
旧路由或旧第三方后端的依据。`ipc_camera` 主要作为架构边界参考，`my_video` 主要作为
板端 bring-up 和协议可跑路径参考；具体契约仍以本项目模块文档为准。

可吸收经验：

- HISI SDK 必须隔离在平台边界。`ipc_camera` 的 platform/media 分层证明，上层模块只消费
  产品 DTO 和窄接口后，HTTP、WebRTC、ONVIF、AI 都能独立演进；`live_stream` 中该经验
  落到 `hisi_vendor -> device -> media -> protocol` 链路。
- MPP bring-up 仍以海思 sample 顺序为真相来源。`my_video` 的 SYS/VB、VI/ISP、
  VPSS、VI->VPSS、VENC、取流线程路径可作为板端排障检查表，但不能把 sample 控制流
  直接暴露给 Web/API。
- 启动失败和停止必须按资源创建反序回滚。原型项目里 media、RTSP、HTTP 的顺序启动、
  反序停止适合作为组合根基线；`live_stream` 要进一步做到中途失败按已启动项逐项回滚，
  `Stop()` 可重复调用。
- 配置保存语义必须是 verify/apply/save，而不是只写 JSON。`my_video` 的配置回调方向
  正确，但全局 `ConfigManager` 和回调散落会放大耦合；本项目按拥有模块校验和应用，
  再由 HTTP/Web 展示明确错误。
- 热路径要用固定上限、引用保活和慢客户端策略。原型里的固定队列、旧帧覆盖、RTSP
  TCP interleaved 和 H.264 AnnexB 解析说明这些问题必须在媒体核心解决；本项目不再让
  协议模块各自维护私有 GOP cache 或无界 socket buffer。
- Web 页面应按 IPC 设备页组织信息。首屏预览、设备摘要、业务分组、调试项下沉、
  区分已生效和待生效状态，比研发调试页式堆参数更利于现场排障。

明确不迁移的教训：

- 不迁移 `my_video` 的全局 `g_media`、`g_rtsp`、单例配置中心、HTTP 直接 include
  media internal 的形态；这些做法适合原型跑通，不适合作为长期边界。
- 不恢复 `ipc_camera` 早期 active/reference 横切文档体系；长期契约继续写入
  `docs/libs/<module>.md`，跨模块阶段计划写入本文。
- 不保留 metaRTC/Yang 后端名、旧 WebRTC signaling alias、旧 `/api/hls`、`/api/flv`
  或 `/api/mjpeg` 兼容路径。
- 不为旧命名或旧目录新增 wrapper、adapter、legacy bridge；需要收敛时同步改调用方、
  文档、配置样例和 Web 类型。

落地检查顺序：

1. `hisi_vendor/device`：先确认 MPP 创建/销毁、锁顺序、抓图/overlay/AI 抓帧与重建互斥。
2. `media/media_codec`：再确认帧进入项目内存后的引用模型、缓存上限、NAL/parameter set
   解析和 timestamp reset。
3. `rtsp/webrtc/http_media/net`：然后确认订阅、RTP packet view、socket 背压和关闭路径。
4. `http/www`：最后确认 API 错误、播放 URL、状态字段和页面提示都来自后端权威状态。

## 5. 重构阶段

### P0：冻结契约和基线

目标：停止计划漂移，先把当前主线解释清楚。

任务：

- 冻结 public header、HTTP/Web API schema、配置字段和事件 payload。
- 明确旧接口、旧命名、旧路径是否删除，不新增兼容层。
- 更新目标模块文档，写清状态来源、资源边界和失败边界。
- 建立旧名扫描和任务模板。

验收：新任务能明确归属模块；API/config/Web/mock/doc 不再分批漂移。

### P1：低风险实现债

目标：消除最影响可读性和可诊断性的实现债。

任务：

- 删除构造后必然非空对象的 `impl_ != nullptr` 防御噪音。
- 给 `Prepare/Start/Apply` 失败点补明确错误日志。
- 减少热路径大对象复制和无意义临时对象。
- 收敛残留手动引用/计数边界。
- 清理重复读锁方法，但保留业务语义明确的 public 函数名。

验收：失败日志能定位具体失败点；热路径没有新增普通日志、大对象复制或 C 风格释放路径。

### P2：HTTP handler 和依赖边界

目标：让 HTTP 控制面依赖必要角色，不再成为全系统上帝入口。

任务：

- 继续减少 HTTP 内部宽构造参数，避免系统状态入口变成新的上帝视图。
- 如需继续收敛低层 callback 风格，优先在 `net` 契约层单独评估，不在 HTTP handler
  内新增适配层。
- HTTP 对 RTSP/WebRTC/ONVIF 的依赖收敛为只读诊断/会话视图接口。
- 统一 API envelope、错误码、参数校验和 URL helper。

验收：handler 构造签名能看出职责；Web 类型、hooks、mock 和模块文档同步。

### P3：media 资源预算和并发边界

目标：把实时预览热路径变成可控资源模型。

任务：

- 统一 `MediaCacheLimits`，覆盖 GOP、subscription、HLS segment、FLV cached tag、shared frame 的 frame/bytes 上限。
- `FrameRing`、`GopCache`、`HlsMaker`、`PreviewClients` 从硬编码常量迁到预算参数。
- 继续收窄主/子码流、HLS、FLV、MJPEG、订阅统计的锁边界。
- 慢客户端触顶后主动断开，保证 reader/client/subscription/cache 计数回落。

验收：多客户端同拉时内存可解释；频繁连接断开后 RSS、fd、client、subscription、cache 回落。

### P4：device / hisi_vendor 生命周期

目标：硬件资源生命周期留在最懂硬件的模块内。

任务：

- 保持 `hisi_vendor` 窄接口：system、pipeline、venc stream、region、snapshot、image、AI runtime。
- `DeviceMedia` 只编排设备业务，不暴露 SDK 内部资源。
- 视频配置热应用失败必须回滚，Web 保存状态和硬件运行态保持一致。
- 抓图、overlay、AI 抓帧在 pipeline 重建期间必须被 device 生命周期挡住。
- 梳理 SDK 锁顺序，避免抓图锁、控制锁、VENC 取流线程、ISP 线程死锁。
- MPP cleanup 失败必须 fail fast，不伪装成已清理。

验收：抓图、OSD、隐私遮挡、图像配置、VENC stream 启停正常；配置失败恢复路径可解释。

### P5：app 组合根和生命周期

目标：去掉隐式全局运行态，建立可回滚的启动/停止流程。

任务：

- 去掉 `Application::Get()` 和 subsystem `Get()` 单例。
- `Application` 持有 subsystem 实例，Start 失败按已启动项反序回滚。
- Stop 可重复调用。
- 信号处理只做 async-signal-safe 标记，正常退出由主循环触发 Stop。

验收：正常启动停止、启动中途失败、重复 Stop、SIGINT/SIGTERM 都能按预期处理。

### P6：协议输出收敛

目标：各实时协议统一到清晰的 reader/cache/subscription 模型。

任务：

- 修复 RTSP wire 兼容问题，优先 VLC/ffplay TCP/UDP 拉流。
- 统一 RTP packet 输出层，RTSP/WebRTC 共享 H.264/H.265 packet view、时间戳和 payload 切片。
- HLS playlist/segment 保持短响应，不作为常驻 session 展示。
- HTTP-FLV、HLS、MJPEG、WebRTC 均通过 `media` 明确接口消费数据。
- WebRTC session、transport、RTP sender 边界继续拆清，不引入 TURN/SFU/datachannel。

验收：WebRTC、RTSP、HTTP-FLV、HLS、MJPEG 首帧、播放、切流、断开恢复正常。

### P7：AI 拆分和可选 VDS 验证

目标：AI 可选、可诊断，不影响直播主链路。

任务：

- 继续拆清 `AiTaskRunner` 的任务调度、抓帧、推理、告警输出和状态统计。
- NNIE 后端继续拆 workspace、输入转换、VGS/IVE、SSD 后处理。
- `motion_classification`、`occlusion_detection` 保持不依赖 `.wk` 的能力边界。
- release 包不承诺发布 `.wk` 模型；模型任务只有模型部署时才可用。
- AI 失败只更新 AI 状态，不影响直播、抓图和配置保存。

VDS 可选路线：

- 不把 VDS `.wk` 直接塞给现有 `ai.model_path`。
- 先做独立 `vds_probe`，验证 `SetCfgFile/Init/GetCap/CreateHandle/UnInit`。
- 再接 `ProcessImageMulti/GetResults/ReleaseResults` 和单帧 YUV 输入。
- 两阶段都通过后，再新增可选 VDS 后端并映射为 `AiDetection`。

验收：`/api/ai/status` 反映真实任务、后端、最近结果和失败原因；关闭或热应用失败时释放推理后端和抓帧资源。

### P8：配置、metrics 和回调契约

目标：统一错误表达和观测入口，但不一次性推倒 DTO。

任务：

- 配置校验统一 `scope`、`field`、`message`、`code`，不急于引入完整 JSON Schema。
- 各模块 `Verify/Apply` 保持归属模块内，但错误码和日志格式一致。
- metrics 先新增内部聚合视图，不替换现有 `Stats/Info` DTO。
- `RequestKeyframeFn`、`MediaFrameCallback` 后续改为窄接口或 callback 对象。
- net/http 低层 `void* user` 可暂留事件边界，业务 handler 不再直接暴露 thunk。

验收：配置热更新失败能定位 scope/field/reason；Web status 和 media sessions 能展示关键指标。

### P9：命名、风格和文档清理

目标：把历史重构留下的风格债一次收口。

任务：

- 清理旧 `_service`、`stream_*`、`MetaRtc/metaRTC/Yang` 和只转调旧接口的 wrapper。
- 统一 header guard、文件命名、注释语言和版权头策略。
- operation audit 和 process log 明确命名边界。
- 清理空目录和临时历史文档。
- 模块契约写入 `docs/libs/<module>.md`，跨模块执行口径写回本文。

验收：旧名扫描不再出现目标架构外的历史命名；文档入口不再冲突。

## 6. 热路径资源和内存模型

本节合并原热路径内存专项的跨模块结论。具体实现仍归拥有模块：
`device`、`hisi_vendor`、`media`、`media_codec`、`net`、`http_media`、`rtsp` 和
`webrtc`。

### 6.1 视频热路径链路

主码流和子码流走同一套热路径，只是 `StreamId`、VENC channel、缓存状态和
client/subscription 计数分开维护。下游协议不能直接订阅 HiSilicon SDK，必须通过
`device -> media` 消费同一份归一化编码帧。

```text
VENC stream
  -> hisi_vendor copies VENC packs into MediaBuffer
  -> device wraps MediaFrame and pushes FrameSink
  -> media normalizes timestamp, parses NAL views, updates GOP/HLS/FLV/MJPEG/subscription caches
  -> http_media / rtsp / webrtc consume slices, segment refs or frame subscriptions
  -> net send queue / UDP endpoint
  -> clients
```

关键生命周期：

- VENC pack 归 MPP stream buffer 管理，`HI_MPI_VENC_ReleaseStream` 后不能继续引用。
- `hisi_vendor` 必须把一个 frame 的多 pack 和环形 buffer 回绕 slice 拼成项目自己的连续
  `MediaBuffer`，这是进入项目内存的必要 payload 深拷贝点。
- `device` 用 `MediaFrame` 描述 `MediaBufferRef payload + codec + pts/dts + frame_type`。
- `media` 后续只做 `MediaFrame` 值拷贝和 `MediaBuffer` 引用计数，不按协议或客户端复制整帧。
- codec 切换、stream stop、时间戳回退或大跳变必须重建参数集、GOP、HLS、FLV、MJPEG
  latest frame 和 subscription live queue。

### 6.2 Payload 拷贝模型

只统计视频 payload 大块拷贝；协议 header、NAL length、HTTP header、RTP/FU header、
FLV timestamp rebase 等小块复制不计入。

| 阶段 | Payload 深拷贝次数 | 说明 |
| --- | ---: | --- |
| VENC pack -> `MediaBuffer` | 1 | 必要拷贝；复制后尽早释放 VENC stream，避免硬件 buffer 被慢客户端拖住。 |
| `device` 分发 | 0 | `MediaFrame` 值拷贝只增加底层 `MediaBuffer` 引用。 |
| `media` ingest / parse / cache | 0 | NAL parser、GOP cache、subscription queue、FLV cache 共享 payload。 |
| HLS 封装 | 1 | TS segment 必须自包含，PES/TS header 和 NAL payload 写入独立 segment body。 |
| HLS HTTP 发送 | 0 | TS segment 由 owner 保活后进入 net queue。 |
| HTTP-FLV live/cache | 0 | tag 小头部复制，视频 payload slice 指向原 `MediaBuffer`。 |
| RTSP RTP packetize | 0 | RTP/FU header 是小 slice，payload 仍指向原帧；TCP interleaved 用 owner 保活。 |
| WebRTC RTP packetize | 0 | RTP packet view 不复制媒体 payload。 |
| WebRTC SRTP protect | 1/packet | libsrtp 需要连续可写 buffer；每个 RTP packet 复制后原地加密。 |
| `net` TCP send queue | 0 或小块复制 | 带 `MediaBufferRef` 的媒体 slice 不复制；无 owner 的栈上 header 会复制。 |

设计取舍：

- HLS segment 是独立 HTTP 对象，必须保存完整 TS body，不能引用原始 frame payload。
- FLV 和 RTP 是流式发送格式，优先用 header slice + payload slice。
- WebRTC 的 SRTP 拷贝是加密边界要求，不长期保存原始 `MediaFrame` 指针。
- 慢客户端、pending bytes、send queue 上限和关闭策略归 `net/http`，不是协议模块各自堆 socket buffer。

### 6.3 协议缓存和封装边界

HLS：

- 只在关键帧边界开始和切分 segment。
- 首个 segment 前的 P/B 帧丢弃。
- H.264 关键帧缺 SPS/PPS、H.265 关键帧缺 VPS/SPS/PPS 时，在 segment 边界前置已缓存参数集。
- playlist 只暴露 finalized segment，半成品 segment 不对外服务。

HTTP-FLV：

- 新客户端先收到 HTTP streaming header、FLV file header、sequence header，再从 cached GOP
  的关键帧起点进入 live tag。
- 每个连接把 timestamp rebase 到从 0 开始，只复制小 header 来重写 timestamp，payload 仍引用原帧。
- H.264 使用 AVCDecoderConfigurationRecord；H.265 使用 enhanced FLV `hvc1` sequence header。

RTSP / RTP：

- RTP timestamp 使用 90kHz clock，由 corrected PTS 转换。
- H.264 超 MTU 使用 FU-A，H.265 超 MTU 使用 FU。
- TCP transport 使用 RTSP interleaved framing，UDP transport 聚合 RTP slices 为 datagram。

WebRTC / SRTP：

- WebRTC 复用 RTP packetizer，payload type、SSRC、clock rate 来自 peer 运行态。
- peer 未见关键帧时丢弃非关键帧，subscription 溢出后等待下一个关键帧恢复。
- SRTP protect 后通过 ICE selected pair 的 UDP socket 发送。

### 6.4 资源上限和观测指标

新增缓存或队列必须先定义上限，再补拥有模块文档。

当前资源上限来源：

- `MediaStreamsOptions::cache_limits`：GOP、HLS segment、FLV cache、shared live frame 等媒体缓存。
- `MediaStreamsOptions`：client/subscription 数量上限。
- `HttpOptions`：HTTP 层连接和 streaming 执行队列。
- `TcpListenOptions`：`send_queue_capacity`、`send_buffer_limit_bytes`、
  `send_stall_timeout_ms`、`read_timeout_ms`、`write_timeout_ms`。
- WebRTC options：peer/session 相关上限。

优化前后至少记录：

| 指标 | 观察点 | 归属模块 |
| --- | --- | --- |
| 进程 RSS / VmHWM | 单客户端、满客户端、慢客户端断连后 | `app` / `http` |
| HLS segment 数量和 body 总量 | segment count/bytes 上限是否生效 | `media` |
| FLV cached tag / GOP 数量 | 新客户端起播缓存是否收敛 | `media` |
| Shared live frame cache | 触顶后慢客户端是否等待关键帧恢复 | `media` |
| 活跃 FLV/MJPEG/frame subscription 数 | 是否受 client/subscription 上限限制 | `media` |
| TCP active connections / pending bytes | 慢 socket 是否触顶后断开 | `net` |
| TCP slow closes / close reason | 队列满、send stall、读写 timeout 是否可区分 | `net` |
| WebRTC peer 数和帧 fanout | peer 增加时是否线性放大持帧时间 | `webrtc` |

`MediaStreamStats::cached_bytes` 继续作为 media 内 GOP、HLS segment 和 FLV GOP cache 的
当前字节近似汇总；细分字段用于定位触顶来源。

### 6.5 热路径验收口径

- 单路主码流 + 子码流预览时，HLS/FLV/MJPEG/WebRTC 任一模式启停后资源应回落到稳定值。
- 客户端达到上限时，新连接失败必须可解释，不能突破 registry 或 HTTP connection 上限。
- 慢客户端断开后，`net` send queue、stream executor backlog 和媒体缓存引用必须释放。
- codec 在 H.264/H.265/MJPEG 间切换后，旧 parameter set、GOP cache 和 segment cache
  不得继续服务新客户端。
- 时间戳回退或大跳变后，GOP、HLS、FLV、MJPEG latest frame 和 subscription live queue
  必须按 reset reason 回落，后续客户端从新的可解码点开始。
- 优化前后必须做聚焦构建和板端预览验证，不能只降低分配却破坏可播放性。

### 6.6 当前热路径重点

- `media`：HLS segment retain、FLV cached tags、GOP cache、MJPEG latest frame、
  shared live frame 和 `MediaFrame` 引用释放。
- `media`：下游 client registry 和 frame subscription 数量上限。
- `media`：HLS/FLV 封装输出减少临时大 buffer。
- `media_codec`：RTSP/WebRTC RTP packet view 避免复制 media payload。
- `net`：慢客户端断连、TCP pending bytes、send queue 和 UDP endpoint 生命周期。
- `http_media`：stream executor 队列和 HTTP streaming session 释放。
- `webrtc`：peer fanout 和 frame dispatch 内存峰值。

## 7. 后续功能路线

这些功能不是基础重构前置条件，但应借重构成果逐步落地。

优先级：

1. 预览链路诊断面板。
2. 一键导出诊断包。
3. 智能画面诊断。
4. 低照图像自动策略增强。
5. 码流质量自适应。
6. AI 事件联动增强。
7. 周界可视化、隐私遮挡增强、画面变化时间线、设备健康监控。

功能边界：

- 预览链路诊断只展示协议状态、首帧、连接、最近错误和缓存水位，不新增协议能力。
- 诊断包只读导出版本、配置、媒体状态、AI 状态、升级状态和最近日志，必须脱敏。
- 智能画面诊断优先用亮度统计、帧差、清晰度、ISP 曝光和 YUV 抓帧，不依赖大模型。
- 低照策略只调整图像参数，不改变编码协议和输出分辨率，必须节流和可回退。
- 码流质量自适应初期只作用于子码流，必须通过 video 配置应用路径。

## 8. 执行顺序和并行规则

必须串行：

1. 契约冻结。
2. public header 和 HTTP/Web API schema 变更。
3. app 组合根大改。
4. 最终旧路径删除和质量门禁。

可以并行：

- `media/device` 核心收敛。
- `net` 背压和连接生命周期。
- `media_codec` RTP 工具层。
- `http_media`、`rtsp`、`webrtc` 在接口冻结后并行。
- `http` 控制 API 和 `www` 在 schema 稳定后并行。
- AI 后端拆分和 AI 前端页面拆分单独排期。
- 预览诊断和诊断包在 API 契约明确后并行。

禁止：

- 不修改 `3rdparty/ZLMediaKit`。
- 不复制 ZLMediaKit 源码片段。
- 不把测试目录迁移混入生产代码命名重构。
- 不把板端验证、协议兼容修复和大范围命名重构混成一个提交。
- 不刷新质量基线来掩盖新增问题。

## 9. 验收计划

本地构建：

```sh
make -j2
make host-test
make -C libs/media
make -C libs/device
make -C libs/hisi_vendor
make -C libs/net
make -C libs/http
make -C libs/http_media
make -C libs/rtsp
make -C libs/webrtc
make -C libs/media_codec
make -C libs/ai
```

Web：

```sh
cd www
npm run build
```

质量扫描：

```sh
python3 scripts/scan/quality_scan.py quick --scope changed --baseline scripts/scan/quality_baseline.json
```

板端和协议：

- 程序启动，HISI MPP 初始化正常。
- 主码流、子码流正常出帧。
- WebRTC 首帧、ICE connected、DTLS connected、SRTP RTP 收帧、关闭恢复正常。
- VLC/ffplay RTSP TCP/UDP 拉流正常。
- HTTP-FLV 首屏、低延迟、断线重连正常。
- HLS playlist/segment 连续播放、刷新、切流正常。
- MJPEG 正常。
- ONVIF 发现设备后返回 RTSP URL 可播放。
- 抓图、OSD、隐私遮挡、区域叠加、关键帧请求正常。
- `/api/media/streams`、`/api/media/sessions`、`/api/system/overview`、`/api/ai/status`、`/api/alarm/status` 正常。

资源稳定性：

- 单路多客户端 HLS/FLV/RTSP/WebRTC 同时拉流。
- 慢客户端发送队列触顶后主动断开。
- 频繁连接/断开后 RSS、fd、reader/client/subscription/cache 回落。
- 长时间播放 PTS/DTS 单调、无花屏、无 segment 空洞。
