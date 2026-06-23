# 重构计划总览

本文是 `docs/refactor/` 下历史重构文档的合并入口。后续工程代码重构、编码、评审和
任务拆分以本文为标准；原始分散文档已合并到本文。

## 已合并来源

以下历史文档已合并到本文并删除：

- `重构.md`：第一阶段总目标、命名标准、任务拆解、ZLMediaKit 对齐要求。
- `重构_2.md`：第二阶段媒体内核、HTTP/Web API、并行任务计划和验收。
- `协议.md`：RTSP、HTTP、HTTP-FLV、HLS、WebRTC、RTP、MediaSource 对齐计划。
- `Media重构.md`：`media`、`device`、`hisi_vendor/platform_hisi` 边界和命名。
- `net.md`：`libs/net` 的事件循环、fd 生命周期、发送队列和背压计划。
- `AI.md`：AI 后端和 AI 告警前端拆分建议。
- `命名扫描.md`：命名收敛必改、保留和延后项。
- `development.md`：多轮重构后的开发经验、任务模板、DoD、质量门禁和日常流程。

## 基线结论

本工程不嵌入 ZLMediaKit SDK，不启动 ZLMediaKit MediaServer 旁路进程。ZLMediaKit 和
ZLToolKit 只作为结构、协议行为和资源模型参照。

产品范围保持 IPC video-only：

- 做实时预览、抓图、配置、告警、运维管理。
- 做 HLS、HTTP-FLV、MJPEG、RTSP、WebRTC 的视频预览链路。
- 不新增音频、录像、存储回放、推流、代理、TURN/SFU、datachannel 或通用媒体服务器控制台。
- 不为旧 HTTP/Web API、旧模块名和旧内部接口长期保留兼容适配层。

架构冲突时优先遵守以下顺序：

1. 当前仓库已经落地的新模块和新命名。
2. 本文的目标边界和执行顺序。
3. 历史文档中的细节说明。

## 目标架构

| 模块 | 应拥有的职责 | 不应拥有的职责 |
| --- | --- | --- |
| `app` | 组合设备、媒体、协议、HTTP、AI、告警等运行期对象 | 媒体缓存、协议组包、硬件细节 |
| `libs/device` | 设备媒体链路、图像、区域、抓图、硬件配置业务对象 | 协议输出、HTTP streaming、reader fanout |
| `libs/hisi_vendor` | HiSilicon SDK 适配、MPP/IVE/VGS/NNIE 低层封装 | 业务状态、HTTP API、媒体订阅策略 |
| `libs/media` | 通用媒体帧、GOP/起播缓存、订阅、HLS/FLV/MJPEG 缓存、统计 | 设备配置、socket、HTTP request 解析 |
| `libs/media_codec` | H.264/H.265 AnnexB、AVCC/HVCC、SPS/PPS/VPS 工具、RTSP/WebRTC 共用 RTP packet view、时间戳、payload 切片 | RTP session、协议连接生命周期、WebRTC transport |
| `libs/net` | EventLoop、Timer、TcpServer、TcpConnection、TcpClient、SocketUtil、send queue、TCP 背压观测 | HTTP、鉴权、媒体业务、配置逻辑 |
| `libs/http` | 控制 API、请求解析、鉴权、短响应、统一错误 envelope | HLS/FLV/MJPEG 长连接输出 |
| `libs/http_media` | HLS、HTTP-FLV、MJPEG、WebRTC signaling/WHEP | 控制 API、设备 SDK 配置、socket 队列管理 |
| `libs/rtsp` | RTSP session、SDP、SETUP/PLAY、RTP over TCP/UDP、RTCP | HTTP API、设备配置、媒体帧缓存所有权 |
| `libs/webrtc` | SDP、ICE、DTLS、SRTP、RTP sender、peer/session 生命周期 | 云信令、公网 relay、datachannel |
| `libs/onvif` | ONVIF 发现、设备服务、返回 RTSP URL | 媒体缓存、RTSP 传输实现 |
| `www` | IPC 管理台和预览 UI | 通用媒体服务器控制台 |

## 命名规则

### 外部基准

命名约束参考两份使用范围广、长期维护的公开规范，并按本项目现有 C++/TypeScript
风格落地：

- [Google C++ Style Guide: Naming](https://google.github.io/styleguide/cppguide.html#Naming)
- [Google TypeScript Style Guide: Identifiers](https://google.github.io/styleguide/tsguide.html#identifiers)

外部规范只作为基准，不直接覆盖本仓库已经形成且一致的命名。例如本项目 C++ public
函数保持 `UpperCamelCase`，Web React 页面和组件文件保持 `UpperCamelCase.tsx`。

### 命名矩阵

| 范围 | 规则 | 示例 | 禁止/避免 |
| --- | --- | --- | --- |
| C++ 目录 | `lower_snake_case`，表达模块或领域 | `http_media`、`media_codec` | `_service` 临时后缀 |
| C++ 文件 | `lower_snake_case.h/.cpp`，按主要职责命名 | `media_streams.cpp`、`net_stat.cpp` | `utils` 兜底名、旧 `stream_*` |
| C++ 类型 | `UpperCamelCase` | `MediaStreams`、`NetStatOptions` | 缩写堆叠、模糊 `Manager` |
| C++ 函数/方法 | `UpperCamelCase`，动宾表达行为 | `SubscribeFrames()`、`GetSnapshot()` | 只转调旧接口的兼容名 |
| C++ 变量/参数 | `lower_snake_case` | `stream_id`、`pending_bytes` | 拼音、无语义缩写 |
| C++ 成员变量 | `lower_snake_case_` | `media_streams_`、`last_seen_ms_` | 无 `_` 后缀的私有成员 |
| C++ 常量 | `kUpperCamelCase` | `kServiceName`、`kMaxRecommendations` | 魔法数字散落 |
| C++ enum 值 | `kUpperCamelCase` | `kNormal`、`kConstrained` | 与类型名重复过长 |
| C++ namespace | `lower_snake_case` | `live_stream` | 模块私有逻辑泄露到顶层 |
| TS 类型/接口/类 | `UpperCamelCase` | `ImageConfig`、`PreviewState` | `I` 前缀泛滥 |
| TS React 组件 | `UpperCamelCase` | `ImageConfigPage` | 组件名和文件名不一致 |
| TS 函数/变量 | `lowerCamelCase` | `buildPreviewUrl`、`streamInfo` | 无动作含义的 `handleData` |
| TS hook | `useUpperCamelCase` | `useImageConfig` | 非 hook 使用 `use` 前缀 |
| TS 常量 | 局部 `lowerCamelCase`，跨模块不变量可 `UPPER_SNAKE_CASE` | `defaultConfig`、`API_TIMEOUT_MS` | 所有 const 都大写 |
| TS 文件 | 页面/组件 `UpperCamelCase.tsx`，hooks/api/utils 用语义 lower camel | `StreamInfoPage.tsx`、`usePreviewPlayer.ts` | 同目录同概念大小写混用 |
| CSS 文件 | 语义化 lower kebab | `ai-alerts.css` | 按颜色或临时页面状态命名 |

### 选择名字的顺序

1. 先用业务领域词：`media`、`device`、`stream`、`subscription`、`session`、`transport`。
2. 再表达对象生命周期：`Start`、`Stop`、`Attach`、`Detach`、`Subscribe`、`Unsubscribe`。
3. 再表达数据形态：`Info`、`Stats`、`Options`、`Dependencies`、`Snapshot`。
4. 最后才使用通用后缀，且必须有明确边界：`Context`、`State`。

`Context` 只用于请求、解析或短生命周期携带信息；`State` 只用于内部状态机或锁内状态。
不新增 `Runtime`、`Manager`、`Store`、`Topology` 这类看起来大但边界不清的名字；
历史已有名字在重构触及时改成具体业务名。

### 必须遵守

- 模块目录和库名不使用不必要的 `_service` 后缀。
- 跨模块设备公共类型使用完整 `Device`，不使用 `Dev`。
- `device/` 模块内部避免重复加 `Device`，例如 `Hardware`、`VideoConfig`、`ImageConfig`。
- 媒体公共类型使用 `Media*`，不用 `Live*`。
- 保留 `FrameType`，不改成 `FrameKind`。
- 当前产品没有音频，视频 codec 可用 `Codec` 表达，不强行保留 `VideoCodec`。
- `VideoBuffer` 统一为 `FrameBuffer`，`BufferSlice` 统一为 `FrameSlice`。
- 对外展示/查询用 `Info`，聚合运行数据用 `Stats`，不用 `Status`、`Counters` 混用。
- 内部锁内变量和状态机可以使用 `State`，但不要进入新的 public API。
- 帧订阅统一使用 `FrameSubscription`、`SubscribeFrames(...)`、`UnsubscribeFrames(...)`。
- 删除旧 `stream_*`、`MetaRtc*`、`Yang*` 和只转调旧接口的临时命名。

### 已明确保留

- ONVIF 的 `device_service` 路径、端口字段和协议概念名，这是规范概念。
- `RequestContext`、HTTP handler、网络 event handler、操作日志 `OperationResult`。
- `SubscriptionStart`、`SubscriptionInfo`、`SubscriptionClose`。
- 内部生命周期状态机中的 `State`。

### 已处理命名清单

- `AiBackendName()` 改为 `AiBackendToString()`。
- Web 类型 `AiBackendName` 改为 `AiBackendId`。
- `MediaFlvStartData`、`GetFlvStartData()`、`MediaFlvStartDataUnref()` 改为
  `MediaFlvStart`、`GetFlvStart()`、`MediaFlvStartUnref()`。
- FLV 起播上下文变量统一为 `flv_start`；`start_data` 只保留在 subscription 起播上下文。
- 清理 header guard 中残留旧模块名，例如 `LIVE_STREAM_HTTP_SERVICE_*`、
  `LIVE_STREAM_AUTH_SERVICE_*`、`LIVE_STREAM_LOGGER_SERVICE_*` 和
  `LIVE_STREAM_NETWORK_SERVICE_*`。
- Web 预览里的预览可用性使用 `PreviewReadiness`、`buildPreviewReadiness` 和
  `previewLiveProtocols.ts`；AI badge 呈现逻辑使用 `aiAlertBadges.ts`。
- 实时预览访问地址统一使用 `MediaPreviewUrls`、`previewUrls` 和
  `getMediaPreviewUrls()`，不使用容易误解为录像回放的 `Playback`。
- HTTP/RTSP 实时媒体内部流程使用 `PreviewUrlsToJson()`、
  `RequireLiveStreamAuthResponse()`、`StartRtspMediaStream()` 和
  `ArmRtspMediaStream()`。

### 延后命名清单

- `http_handler_utils.*`、`http_request_utils.*`、`http_media_utils.*` 按 response、auth、
  request body、path/stream 语义拆分或重命名。该项会牵动大量 handler include，
  后续应单独执行并同步质量基线。

## 关键设计

### 媒体与设备

目标是把设备采集与通用媒体分发彻底分开：

1. `libs/device` 拥有设备媒体链路、硬件配置、图像配置、区域、抓图、关键帧请求入口。
2. `libs/media` 拥有通用媒体帧、缓存、订阅、协议输出需要的起播数据和统计。
3. `app` 显式组合 `DeviceMedia -> MediaStreams`。
4. HTTP/RTSP/WebRTC 只通过 `media` 获取帧和缓存，不直接依赖设备层。
5. 控制 API 可同时依赖 `device` 和 `media`，但不能绕过模块边界直接改内部缓存。

核心数据流：

```text
HiSilicon MPP -> device -> MediaStreams -> media cache/subscription
                                      -> http_media / rtsp / webrtc
                                      -> http control API stats
```

### 网络基础层

`libs/net` 的目标是小而硬：

1. 只提供 EventLoop、Timer、TcpServer、TcpConnection、TcpClient 和必要 SocketUtil。
2. fd 归属唯一连接对象。
3. 事件回调只在所属 loop 线程执行。
4. 关闭流程统一为 `DisableEvents -> RemoveFromPoller -> CloseFd -> NotifyClosed`。
5. `Send()` 只入队或尝试立即发送；EAGAIN 后监听写事件。
6. 每个连接必须有发送队列上限、读缓冲上限、idle timeout、send timeout。
7. 跨线程只允许 `PostTask()`，不允许多个线程直接操作同一个 socket。
8. 错误语义收敛为项目自己的 `NetErrorCode`，至少覆盖 eof、timeout、reset、
   refused、dns failed、local close 和 system error。

`net_stat.h` 属于 `libs/net`，只做观测和建议，不直接拥有连接关闭权。

### 协议栈

协议模块按职责拆开：

- `media_codec` 提供 H.264/H.265 RTP packet view、payload 切片和时间戳辅助。
- `rtsp` 处理 RTSP 方法、SDP、RTP over TCP/UDP、UDP peer 学习、RTCP 统计。
- `http` 处理控制 API、鉴权、短响应和统一错误 envelope。
- `http_media` 处理 HLS、HTTP-FLV、MJPEG、WebRTC signaling/WHEP。
- `webrtc` 拆为 signaling/service、session、transport、RTP sender。

协议优先级：

1. 先修可复现兼容问题，尤其是 VLC/ffplay RTSP 拉流。
2. 再统一 RTP packet 输出层。
3. 再收敛媒体 reader/ring 分发。
4. 最后补协议诊断和资源统计。

暂不处理：

- WebRTC 公网扫码、腾讯云信令、TURN/SFU/relay、设备云绑定。
- RTMP、GB28181、MP4、录像、回放、推流、代理。
- ZLMediaKit 的 hook、room keeper、通用 server 管理台。

### HTTP/Web API

目标是让 API 表达产品能力，而不是暴露内部模块结构：

- 控制 API 归 `http`。
- 媒体输出 API 归 `http_media`。
- API response 使用统一 envelope、错误码和参数校验。
- `/api/media/streams` 展示 stream info、codec ready、subscription/client count、缓存统计。
- `/api/media/sessions` 聚合 RTSP、WebRTC、HTTP-FLV、HLS、MJPEG session/subscription 状态。
- 后端 URL helper 负责生成 RTSP/HLS/FLV/MJPEG/WebRTC URL，Web 不拼协议细节。
- HLS playlist/segment 是短响应，不作为常驻 session 展示。

### Web Console

Web Console 是 IPC 管理台，不做通用媒体服务器控制台：

- 预览页只展示本设备 video-only 实时预览能力。
- 协议选择、连接状态、首帧、断线重连、错误信息应清楚可诊断。
- AI 告警页面应从大页面拆成组合层，复用 `features/ai-alerts` 下已有组件。
- API DTO 变化必须同步 Web 类型、hooks 和页面。

### AI

AI 当前风险集中在职责过厚：

- AI 运行态对象同时管理配置热更新、任务生命周期、采集、推理、告警注入、快照和统计。
- `nnie_engine.cpp` 同时处理模型加载、workspace、VGS/IVE 转换、SSD 后处理。

拆分方向：

- 任务调度/运行态。
- 告警输出。
- NNIE workspace。
- 输入转换。
- SSD 解码。
- 前端告警页面组合层。

AI 拆分不要和媒体/协议主干大改混在同一提交里。

## 执行阶段

### 阶段 0：冻结契约

- 冻结模块边界、public header、HTTP/Web API schema、命名规则。
- 建立旧名扫描清单。
- 明确哪些历史 API 不保留兼容。
- 产物：契约文档、扫描脚本、任务卡模板。

### 阶段 1：命名和模块边界清理

- 清理 `_service` 后缀、旧 header guard、旧文件名和旧 public API。
- 收敛 `media`、`device`、`http_media`、`net_stat` 等模块文档。
- 保持每次提交只做一个命名主题，避免混入行为改动。

### 阶段 2：媒体/设备核心收敛

- 设备采集、图像、抓图、区域归 `device`。
- 通用帧、缓存、订阅、慢客户端统计归 `media`。
- `app` 负责装配 `DeviceMedia -> MediaStreams`。
- 协议模块只消费 `media` 提供的 frame/cache/subscription。

### 阶段 3：网络背压和连接生命周期

- 补齐 send queue、pending bytes、超时、close reason。
- 建立慢客户端断开和恢复窗口。
- 所有协议模块通过统一网络接口感知写阻塞，不直接操作 fd。

### 阶段 4：协议输出收敛

- 修复 RTSP wire 兼容问题。
- 对齐 UDP peer 学习、RTP over TCP/UDP、RTCP 统计。
- 统一 RTP packet 输出层。
- 收敛 HTTP-FLV/HLS/MJPEG/WebRTC 对媒体缓存的消费方式。

### 阶段 5：HTTP/Web API 和 Web Console

- 统一 API envelope、错误码、URL helper 和 media sessions 诊断。
- Web 类型、hooks、页面同步 API schema。
- AI 告警页面和预览页面按组件职责拆分。

### 阶段 6：AI 拆分

- 拆 AI 运行态对象的内部状态和任务调度。
- 拆 NNIE engine 的 workspace、输入转换、后处理。
- 保持 AI 任务生命周期、告警输出和快照路径可验证。

### 阶段 7：集成、清理和质量门禁

- 删除旧 wrapper、alias、legacy adapter。
- 清理旧名残留扫描。
- 跑构建、前端、静态扫描、板端播放和资源回落验证。

## 并行规则

### 必须串行

1. 阶段 0 契约冻结。
2. public header 和 HTTP/Web API schema 变更。
3. `app` 装配层大改。
4. 最终旧路径删除和质量门禁。

### 可以并行

- `media/device` 核心收敛。
- `net` 背压和连接生命周期。
- `media_codec` RTP 工具层。
- `http_media`、`rtsp`、`webrtc` 在接口冻结后并行。
- `http` 控制 API 和 `www` 在 schema 稳定后并行。
- AI 后端拆分和 AI 前端页面拆分可单独排期。

### 禁改规则

- 不修改 `3rdparty/ZLMediaKit`，只参考结构和行为。
- 不复制 ZLMediaKit 源码片段；如确需复制，必须保留对应 license/copyright。
- 不把测试目录迁移混入生产代码命名重构；确需改测试时单独说明目标。
- 不把板端验证、协议兼容修复和大范围命名重构混成一个提交。
- 不刷新质量基线来掩盖新增问题。

## 开发工作流

本节沉淀项目从初始功能框架到多轮媒体、协议、Web 和设备边界重构后的开发经验。
模块长期设计仍写入 `docs/libs/<module>.md`，不要把模块契约散落到临时文档里。

### 项目经验

Git 历史显示，本项目经历了四个阶段：

- 2026-04-28 到 2026-05-06：功能框架成型，完成基础运行时、Web 控制台和 WebRTC
  预览雏形。
- 2026-05-07 到 2026-05-25：补齐 HLS、HTTP-FLV、RTSP、抓图、升级、AI、配置和
  认证链路，同时暴露出热路径、状态归属和 API 同步问题。
- 2026-06-06 到 2026-06-11：高频架构收敛，集中拆分 media、device、http、rtsp、
  webrtc、net、web 和 app 组合根，删除旧 service/stream/metaRTC/Yang 兼容层。
- 当前阶段：主链路可构建，边界契约基本稳定，后续重点是质量基线、热路径资源模型
  和历史质量债收口。

沉淀出的规则：

- 先冻结契约，再改实现。公共 C++ API、HTTP API、配置字段、Web DTO、事件 payload
  不允许边写边漂移。
- 一轮任务只碰一个主模块，最多带一个相邻接口模块；不要把 bugfix、rename、
  cleanup、refactor 混在一起。
- 状态由最接近真实资源的模块拥有；上层只消费状态，不重复推导。
- 不保留长期兼容层。旧 URL、旧 DTO、旧 wrapper 和旧命名如果不再是目标架构的一部
  分，应在同一任务内删除。
- API、配置、Web、mock、文档必须同批更新；只改后端或只改前端都会制造下一轮考古。
- 帧路径和协议热路径优先检查资源上限、背压、关闭路径、拷贝次数和日志量。

### 任务模板

每个非平凡任务开工前先写清楚以下内容，可放在提交说明、任务描述或临时工作记录里。

```text
目标：
范围：
不做什么：
主模块：
相邻接口模块：
公共契约变化：
配置/API/Web/mock/doc 是否同步：
热路径或资源风险：
验证命令：
回滚方式：
```

填写标准：

- `目标` 写业务结果，不写“优化一波”。
- `范围` 写要改的模块和行为；`不做什么` 写明确排除项。
- `公共契约变化` 没有就写“无”；有则先更新拥有模块文档。
- `验证命令` 至少覆盖构建、changed 质量扫描和受影响模块的测试或 Web 检查。

### Definition Of Done

提交前逐项确认：

- 目标模块和相邻接口模块已经按任务模板收口。
- 构建通过，或失败原因明确且不是本任务新增。
- `python3 scripts/scan/quality_scan.py quick --scope changed` 已运行。
- 涉及 API/config/Web 的改动已同步后端 handler/DTO、`www/src/api/types.ts`、mock、
  `www/README.md` 和拥有模块文档。
- 涉及命名迁移时，生产代码没有新增旧 `*_service`、`stream_*`、`MetaRtc*`、`Yang*`
  或只转调旧接口的兼容 wrapper。
- 热路径没有新增普通日志、无界队列、无界缓存、无说明的大 buffer 拼接或阻塞等待。
- 新增 helper/class/hook 不是只隐藏 2-3 行逻辑的薄包装。

### 质量门禁

质量扫描分三层使用：

- 开发前基线：`python3 scripts/scan/quality_scan.py quick --scope changed`
- 合入前门禁：
  `python3 scripts/scan/quality_scan.py quick --scope changed --baseline scripts/scan/quality_baseline.json`
- 大节点或夜间全量：
  `python3 scripts/scan/quality_scan.py quick --scope all --baseline scripts/scan/quality_baseline.json`

基线策略：

- 历史问题先进入 `scripts/scan/quality_baseline.json`，避免老问题阻塞无关开发。
- 新增 `error` 或 `warning` 默认阻断；新增 `note` 只进入报告。
- 每周从基线里清一批历史问题，优先处理格式噪音、cppcheck error、热路径 sleep/logging、
  flawfinder 真实风险和协议资源释放问题。
- 首次生成或人工确认后刷新基线：
  `python3 scripts/scan/quality_scan.py baseline --from-findings scripts/scan/reports/quality/quality_findings.json --output scripts/scan/quality_baseline.json`
- 刷新前必须确认 `--from-findings` 指向期望的扫描结果；全量基线应来自 `--scope all`
  的扫描结果，不能误用 changed 扫描结果覆盖。
- 不要为绕过门禁刷新基线；只有确认问题属于历史债或已被单独记录时才更新。

### 日常流程

1. 读 `docs/README.md`、本文和目标模块文档。
2. 按任务模板冻结目标、范围、非目标和验证方式。
3. 先改公共契约，再改实现，再同步 Web/config/mock/docs。
4. 小步提交；bugfix、rename、cleanup、refactor 分开。
5. 合入前跑 changed 扫描和目标模块验证。
6. 大重构完成后，把本次新增经验补回本文或拥有模块文档。

### Review 关注点

review 时优先看这些问题：

- 模块是否跨边界读取不属于自己的状态。
- 是否引入长期兼容层或影子 DTO。
- 热路径是否出现无界分配、无界队列、普通日志或阻塞等待。
- 关闭路径是否能取消 timer、detach reader、释放 socket、释放硬件资源。
- Web 状态是否来自后端明确字段，而不是前端重新推导。
- 文档是否记录了新的契约、状态来源、资源上限和失败边界。

## 提交拆分建议

1. `docs(refactor): freeze architecture and naming contract`
2. `refactor(names): remove legacy service and stream names`
3. `refactor(media): split device capture from media streams`
4. `refactor(net): add bounded send queue and close flow`
5. `fix(rtsp): align setup play and udp peer behavior`
6. `refactor(media-codec): share video rtp packet output`
7. `refactor(http-media): isolate streaming handlers`
8. `refactor(webrtc): split session transport and rtp sender`
9. `refactor(http): normalize media api schema`
10. `refactor(www): align console with media api schema`
11. `refactor(ai): split runtime scheduling and inference backend`
12. `chore(quality): remove legacy paths and update baseline`

## 验收计划

### 本地构建

- `make -j2`
- `make host-test`
- `make -C libs/media`
- `make -C libs/device`
- `make -C libs/net`
- `make -C libs/http`
- `make -C libs/http_media`
- `make -C libs/rtsp`
- `make -C libs/webrtc`
- `make -C libs/media_codec`

### Web

- `cd www && npm run typecheck`
- `cd www && npm run lint`
- `cd www && npm run test`
- `cd www && npm run format:check`
- `cd www && npm run build`

### 质量扫描

- `python3 scripts/scan/quality_scan.py quick --scope changed --baseline scripts/scan/quality_baseline.json`
- `python3 scripts/scan/quality_scan.py quick --scope all --baseline scripts/scan/quality_baseline.json`
- 大节点使用 `full` 模式补 scan-build、clang-tidy、include-what-you-use。

### 协议和板端

- 程序启动，HISI MPP 初始化正常。
- 主码流、辅码流正常出帧。
- WebRTC 首帧、ICE connected、DTLS connected、SRTP RTP 收帧、关闭恢复正常。
- VLC/ffplay RTSP TCP/UDP 拉流正常。
- HTTP-FLV 首屏、低延迟、断线重连正常。
- HLS playlist/segment 连续播放、刷新、切流正常。
- MJPEG 正常。
- ONVIF 发现设备后返回 RTSP URL 可播放。
- 抓图、OSD、隐私遮挡、区域叠加、关键帧请求正常。
- `/api/media/streams`、`/api/media/sessions`、`/api/ai/status`、`/api/alarm/status`
  不因重构异常。

### 资源和稳定性

- 单路多客户端 HLS/FLV/RTSP/WebRTC 同时拉流。
- 慢客户端发送队列触顶后主动断开。
- 频繁连接/断开后 RSS、fd 数、reader/client count、帧缓存数量回落。
- 长时间播放 PTS/DTS 单调、无花屏、无 segment 空洞。
- 旧名扫描确认生产代码不恢复 `_service`、`stream_*`、`MetaRtc/metaRTC/Yang`。
- 链接日志确认不再链接 metaRTC/Yang 相关库。

## 后续使用方式

1. 新重构任务先在本文定位所属阶段和模块边界。
2. 任务卡必须写明修改范围、禁止修改范围、验证命令和回滚点。
3. 代码提交前同步更新本文或对应模块文档。
4. 原始历史文档只用于追溯细节；新的执行口径以本文为准。
