# 工程质量与架构问题扫描记录

来源：2026-06-23 全工程代码扫描
范围：`libs/ai/`、`libs/media/`、`libs/http/`、`libs/infra/`、`libs/hisi_vendor/`、`libs/media_service/`、`libs/stream_hub_service/` 等核心模块
目标：识别"代码不像产品级"的具体来源，区分实现细节问题与架构问题

## 一、实现细节问题（low 感来源）

### 1.1 手动引用计数代替 RAII

`libs/media/src/media_streams.cpp` 内有 6 个手动 `Ref/Unref` 函数：

- `MediaFlvCachedVideoTagUnref`
- `MediaFlvCachedVideoTagRefCopy`
- `MediaFlvStartUnref`
- `SubscriptionStartUnref`
- `SubscriptionFrameUnref`
- `MediaSegmentRefUnref`

调用者必须在每条 return 路径手动调用，是 C 风格，不是 C++。
正确做法：定义 `EncodedFrameRef` RAII wrapper 或直接用 `std::shared_ptr<const EncodedFrame>`。

### 1.2 全局变量 + 单 mutex 日志

`libs/infra/src/log.cpp` 用 7 个 `g_xxx` 全局变量：

```cpp
std::mutex g_log_mutex;
std::condition_variable g_log_condition;
LogConfig g_config;
bool g_initialized = false;
bool g_stopping = false;
std::deque<std::string> g_async_lines;
std::thread g_worker;
std::FILE* g_file = nullptr;
```

问题：全局可变状态、不可注入、不可测试、跨模块初始化顺序不确定。
正确做法：封装为 `Logger` 类，通过依赖注入传递。

### 1.3 namespace 别名掩盖命名失败

`libs/media/src/media_streams.cpp:17-18`：

```cpp
namespace source_state = media_internal;
namespace source_clients = media_streams_internal;
```

起名 `media_internal` 又觉得不够清楚再起别名，是命名债的标志。
正确做法：直接命名为 `media_stream_state`、`media_stream_clients`，删除别名。

### 1.4 重复的 lock_guard 模板

`libs/media/src/media_streams.cpp` 中 `IsHlsSupported`、`IsFlvSupported`、`IsMjpegSupported`、`IsStreamAvailable` 等 6+ 方法 99% 代码相同，仅判别函数不同。
正确做法：用模板或 lambda 抽象为 `IsStreamReady(StreamId, IsReadyFn)`。

### 1.5 过度防御性 null 检查

`libs/ai/src/ai.cpp` 中 6 个方法都查 `impl_ != nullptr`：

```cpp
bool Ai::Start() { return impl_ != nullptr && impl_->core.Start(); }
AiCapabilities Ai::GetCapabilities() const {
  return impl_ != nullptr ? impl_->core.GetCapabilities() : AiCapabilities{};
}
```

`impl_` 在构造函数 `new` 出来，构造成功就非空。每个方法都查 nullptr 是噪音。
正确做法：构造失败直接 abort，方法内不查。

### 1.6 缩进风格不一致

`AGENTS.md` 规定 4 空格缩进，但实际：
- `libs/ai/src/ai.cpp` —— 4 空格 ✓
- `libs/media/src/media_streams.cpp` —— 4 空格 ✓
- `libs/media_service/src/media_buffer_pool.cpp` —— 2 空格 ✗
- `libs/hisi_vendor/src/*.cpp` —— 2 空格 ✗

新写的 `hisi_vendor` 已经违反规范，说明没有 `.clang-format` 强制 + CI 检查。

### 1.7 模块目录命名维度混乱

`libs/` 4 种维度混用：

| 目录 | 维度 |
|------|------|
| `ai` `auth` `device` `system` | 业务 |
| `event` `net` `infra` | 技术 |
| `http` `rtsp` `webrtc` `onvif` | 协议 |
| `hisi_vendor` `media_codec` `stream_mux` | 厂商/技术 |

正确做法：按层级分组为 `base/` `net/` `media/` `service/`。

### 1.8 错误信息丢失

`libs/http/src/http_server.cpp::HttpServer::Prepare()` 有 6 个失败点全部返回 `false`：

```cpp
if (net_engine_ == nullptr || request_handler_ == nullptr) return false;
if (net_loop_ == nullptr) return false;
if (options_.max_request_header_bytes == 0 || ...) return false;
```

调用者无法判断是参数错、依赖未注入还是配置错。
正确做法：用 `Result<T, ErrCode>` 或至少每个失败点写 `INFRA_LOG_ERROR`。

### 1.9 单文件职责堆叠

`libs/media/src/media_streams.cpp` 866 行同时包含：
- 6 个引用计数函数
- 命名转换函数（`MediaStreamResetReasonName`、`SubscriptionCloseName`）
- 类型转换函数（`ToMediaFlvVideoTagView`、`CloseReasonForReset`）
- `MediaStreams::Impl` 主类
- 流状态管理、FLV、HLS、MJPEG、订阅、统计、关键帧请求

违反 `AGENTS.md` 自己规定的"按功能职责拆分文件"。

### 1.10 缺少统一 Buffer/Frame 抽象层

`EncodedFrame`、`MediaFlvCachedVideoTag`、`SubscriptionFrame`、`MediaSegmentRef`、`FrameBuffer`、`BufferSlice` 6+ 种帧类型并行存在，每种都有自己的 `Ref/Unref`。
正确做法：定义统一 `Frame` 接口，所有下游吃同一个类型。

---

## 二、架构层面问题

### 2.1 缺少统一的帧数据抽象

**现状**：项目里有 6+ 种帧类型并行存在：

- `EncodedFrame`（VENC 输出）
- `MediaFlvCachedVideoTag`（FLV 缓存）
- `SubscriptionFrame`（订阅者收到）
- `MediaSegmentRef`（HLS 分片）
- `FrameBuffer`（通用缓冲）
- `BufferSlice`（切片引用）

每种有自己的 `Ref/Unref/RefCopy`，互相转换时手动管理。

**应有设计**：一个统一的帧接口，所有消费者吃同一个类型：

```cpp
class Frame {
 public:
  const uint8_t* Data() const;
  size_t Size() const;
  StreamId Stream() const;
  Codec GetCodec() const;
  FrameType Type() const;
  int64_t PtsUs() const;
  void AddRef();
  void Release();
};
using FramePtr = intrusive_ptr<Frame>;
```

下游消费者（FLV/HLS/RTSP/录像）只持有 `FramePtr`，零拷贝、自动回收。

### 2.2 流水线没有 Stage 抽象

**现状**：`MediaStreams::Impl` 一个类做了所有事：

```
接收帧 → 时间戳修正 → NAL 解析 → FLV 打包 → GOP 缓存 → HLS 分片 → 推送订阅者 → 统计
```

500+ 行 Impl 里塞了完整流水线，每个环节互相耦合。

**应有设计**：明确的 Stage 接口，每个 Stage 单一职责：

```cpp
class FrameStage {
 public:
  virtual void Process(FramePtr frame) = 0;
  virtual void OnStreamReset(StreamId, ResetReason) {}
};

class TimestampCorrector : public FrameStage;
class NalParser : public FrameStage;
class FlvPackager : public FrameStage;
class GopCache : public FrameStage;
class HlsSegmenter : public FrameStage;
class SubscriberDispatcher : public FrameStage;
```

收益：每个 Stage 可独立测试、可重排、新增协议只加 Stage 不改主类。

### 2.3 多消费者模型缺失

**现状**：一帧到达后每个订阅者各自拷贝：

```cpp
for (subscriber : subscribers) {
  EncodedFrame copy;
  EncodedFrameRefCopy(&copy, &frame);  // 拷贝引用计数结构
  subscriber->OnFrame(copy);
}
```

N 个订阅者 → N 次引用计数操作 + N 次锁竞争。

**应有设计**：基于引用计数的单帧多消费者：

```cpp
FramePtr frame = VbFrame::Create(vb_blk, ...);  // 引用 = 1
dispatcher_->Dispatch(frame);  // 内部对每个 subscriber 调 frame->AddRef()
```

`intrusive_ptr` 自动管理，零拷贝、零锁。当前手动 Ref/Unref 是这个模型的退化版本。

### 2.4 状态机不明确

**现状**：`MediaStreams::Impl` 只有 `started_` 一个 bool，实际隐含状态：

- 未初始化
- 已 Start 等待首帧
- 正在处理帧
- 收到 Reset 信号（时间戳跳变/编码切换）
- 正在 Stop（清空缓存、通知订阅者）
- 已 Stop

这些用一个 bool 表达，导致每个方法都要重新判断"现在到底处于什么状态"。

**应有设计**：显式状态机：

```cpp
enum class StreamState {
  kIdle, kArming, kStreaming, kResetting, kStopping, kStopped,
};
bool Transition(StreamState expected, StreamState next);
```

### 2.5 配置变更没有版本化模型

**现状**：配置变更通过 ConfigService 回调直接生效：

```
配置改了 → 回调触发 → 直接 Stop/Start pipeline
        → 期间到达的帧丢失
        → 订阅者连接断开
```

**应有设计**：配置 generation + 优雅切换：

```cpp
struct PipelineGeneration {
  uint64_t id;
  MediaPipelineConfig config;
  std::atomic<bool> active{false};
};

void OnConfigChanged(const MediaPipelineConfig& new_config) {
  auto gen = std::make_shared<PipelineGeneration>(...);
  StartNewPipeline(gen);
  WaitForDrain(old_gen);
  active_generation_.store(gen);
}
```

订阅者通过 `active_generation_.load()` 决定吃哪一代 pipeline 的帧，热切换不丢连接。

### 2.6 资源预算没有统一管理

**现状**：各模块各管各的内存：

- `frame_ring`：缓存 N 帧（无上限字节）
- `flv_live_ring`：M 个 client，每个 K 帧
- `gop_cache`：1 个 GOP
- `hls segments`：depth 个分片

总和不可控，OOM 风险。

**应有设计**：全局 ResourceBudget：

```cpp
struct MediaResourceBudget {
  size_t max_frame_buffer_bytes = 16 * 1024 * 1024;
  size_t max_segment_cache_bytes = 32 * 1024 * 1024;
  uint32_t max_subscribers = 32;
  uint32_t max_flv_clients = 8;
};
```

所有缓存向 budget 申请，启动时一次性 mmap/分配，运行时只在预算内分配。

### 2.7 模块间数据契约缺失

**现状**：`MediaService → StreamHub → HttpService` 之间传 `EncodedFrame`，但 `EncodedFrame` 在 `media_service/include/media/encoded_frame.h`，包含 `FrameBuffer`，`FrameBuffer` 又依赖具体的 buffer 池实现。

下游模块为了用 `EncodedFrame` 必须包含 `media_service` 的头文件，耦合到 buffer 实现细节。

**应有设计**：跨模块用纯接口契约：

```cpp
// libs/contracts/include/contracts/frame.h
struct FrameView {
  const uint8_t* data;
  size_t size;
  StreamId stream_id;
  Codec codec;
  FrameType type;
  int64_t pts_us;
};

class IFrameSource {
 public:
  virtual SubscriptionId Subscribe(StreamId, IFrameSink*) = 0;
  virtual void Unsubscribe(SubscriptionId) = 0;
};

class IFrameSink {
 public:
  virtual void OnFrame(FrameView) = 0;  // 数据在回调期内有效
  virtual void OnReset(StreamId, ResetReason) = 0;
};
```

`HttpService` 只依赖 `IFrameSource`，不依赖 `EncodedFrame`。换 buffer 实现不影响下游。

---

## 三、问题汇总表

| # | 类型 | 问题 | 影响 |
|---|------|------|------|
| 1.1 | 实现 | 手动 Ref/Unref 代替 RAII | 最大 low 感来源，C 风格 |
| 1.2 | 实现 | 日志全局变量 | 不可注入、不可测试 |
| 1.3 | 实现 | namespace 别名掩盖命名债 | 命名混乱 |
| 1.4 | 实现 | 重复 lock_guard 模板 | 代码重复 |
| 1.5 | 实现 | 过度防御性 null 检查 | 噪音 |
| 1.6 | 实现 | 缩进风格不一致 | 违反项目规范 |
| 1.7 | 实现 | 模块目录命名维度混乱 | 结构不清 |
| 1.8 | 实现 | 错误信息丢失 | 不可追踪 |
| 1.9 | 实现 | 单文件职责堆叠 | 违反 AGENTS.md |
| 1.10 | 实现 | 缺统一 Buffer/Frame 抽象 | 类型爆炸 |
| 2.1 | 架构 | 缺统一 Frame 接口 | 内存管理混乱 |
| 2.2 | 架构 | Pipeline 无 Stage 抽象 | 不可扩展、不可测试 |
| 2.3 | 架构 | 多消费者模型缺失 | N 倍内存 + 锁竞争 |
| 2.4 | 架构 | 状态机不明确 | 状态判断散落 |
| 2.5 | 架构 | 配置变更无版本化 | 切换时丢连接 |
| 2.6 | 架构 | 无全局 ResourceBudget | OOM 风险 |
| 2.7 | 架构 | 跨模块契约接口缺失 | 模块强耦合 |

---

## 四、建议的优先级

按 ROI 排序：

| 优先级 | 改造项 | 投入 | 收益 |
|--------|--------|------|------|
| P0 | 删除所有手动 Ref/Unref，统一 RAII wrapper | 2-3 天 | 立刻消除最大 low 感 |
| P0 | 加 `.clang-format` + CI 检查 | 0.5 天 | 统一缩进 |
| P1 | `infra/log.cpp` 重构为 `Logger` 类 | 1 天 | 可注入、可测试 |
| P1 | 用模板合并 6 个相同 lock_guard 方法 | 0.5 天 | 减少重复 |
| P1 | 删除过度防御性 `if (impl_ != nullptr)` | 0.5 天 | 代码清爽 |
| P2 | `bool` 失败改为 `Result<T, ErrCode>` 或加日志 | 2 天 | 错误可追踪 |
| P2 | `media_streams.cpp` 按职责拆分文件 | 1 天 | 单文件可读 |
| P2 | 引入统一 `Frame` 抽象（解决 2.1） | 3-5 天 | 多消费者零拷贝基础 |
| P3 | 引入 `FrameStage` 抽象（解决 2.2） | 3-5 天 | 流水线可扩展 |
| P3 | 引入显式状态机（解决 2.4） | 2-3 天 | 状态判断收敛 |
| P3 | 全局 `ResourceBudget`（解决 2.6） | 3 天 | 避免 OOM |
| P4 | 跨模块契约接口（解决 2.7） | 5-7 天 | 模块解耦 |
| P4 | 配置 generation 切换（解决 2.5） | 3-5 天 | 热切换不丢连接 |
| P4 | 模块目录按层级重组（解决 1.7） | 1 天 | 结构清晰但风险高 |

---

## 五、说明

- 本文件仅记录扫描结果，**不替代** `refactor/README.md` 中的重构基准和任务排期
- 命名、风格类问题（1.3/1.6/1.7）应纳入 `refactor/README.md` 的质量门禁统一处理
- 架构类问题（2.1-2.7）需要单独的设计评审，确认目标设计后再启动改造
- 改造应按 P0 → P4 顺序，每完成一个优先级再启动下一个，避免一次性大改导致回归风险


一、命名问题
1.1 模块目录命名维度混乱（最严重）
libs/ 4 种维度混用，没有层级感：

目录	维度
ai auth device system	业务
event net infra	技术
http rtsp webrtc onvif	协议
hisi_vendor media_codec stream_mux	厂商/工具
而 http_media 又是 http 的衍生品，media 和 device 之间职责重叠。

1.2 media 与 device 职责重叠
libs/media/ 有 MediaStreams、FrameRing、FlvMuxer、HlsMaker、GopCache
libs/device/ 有 DeviceMedia、HardwarePipeline、SnapshotCapture、RegionOverlay、MediaChannels
device 内部包了 HardwarePipeline，而 media 才是真正做流管理的。两者关系像"设备封装"和"流分发"，但命名上完全看不出来。

1.3 infra 目录里的 logger.h vs log.h 容易混淆
infra/log.h —— 运行日志（Trace/Debug/Info/Warn/Error/Fatal 宏）
infra/logger.h —— 操作审计日志（Login/Logout/ModifyConfig 等业务动作）
logger 这个名字看起来像 log 的别名或加强版，但实际是完全不同的"用户操作审计"。命名应该改成 audit_log.h 或 operation_log.h。

1.4 类名风格不一致
模块	类名风格	例子
media	PascalCase + 内部 media_internal	MediaStreams、FlvMuxer
device	PascalCase + 内部 device_internal	DeviceMedia、HardwarePipeline
ai	PascalCase 但顶层类只有 Ai、AiCore	Ai、AiCore、AiInferenceEngine
net	PascalCase + 内部 net_internal	NetEngineImpl、TcpSession
http	PascalCase + HttpImpl	HttpImpl、HttpServer、HttpSession
hisi_vendor	PascalCase + MppHisiSdk	MppHisiSdk
但 infra/log.h 里的类叫 Log（不是 Logger），而 infra/logger.h 里的接口叫 ILogger——同一个目录两个日志类用了不同前缀风格。

1.5 函数命名不统一
media/：PushFrame()、AttachFlvClient()、PopSubscriptionFrame() —— Verb+Noun 风格
device/：CaptureSnapshot()、GetStreamCodec()、SetFrameSink() —— 同风格
http/：HandleConfig()、RegisterRoutes() —— 同风格
但 ai/：GetLastResult()、ListAlerts()、ReadAlertImage() —— 没问题
infra/log.cpp：WriteLineLocked()、RotateFileIfNeededLocked() —— Locked 后缀
media/media_streams.cpp：ResetMediaStateLocked() —— 同样 Locked 后缀
但 media/frame_ring.h：AppendToCache()、PushLiveQueue() —— 不加 Locked 后缀，注释说"本类所有方法都应在同一把 mutex 保护下调用"
约定不一致：哪些方法加 Locked 后缀、哪些不加，没有统一规则。

1.6 service vs engine vs server vs core 混用
模块	命名
net	NetEngineImpl、TcpServer
http	HttpServer、HttpImpl
rtsp	RtspImpl
webrtc	WebrtcImpl
event	event::Service、event::Dispatcher、event::Loop、event::Executor
ai	AiCore、AiInferenceEngine
system	ISystem、ISystemPlatform
auth	IAuth
Engine/Server/Impl/Core/Service/Dispatcher 六种后缀混用，没有统一语义。

1.7 internal namespace 命名不一致
media：media_internal
device：device_internal
net：net_internal
rtsp：rtsp_internal
webrtc：webrtc_internal
infra：infra（没有 internal）
但是 media_streams.cpp:17-18 又做了别名：

cpp
namespace source_state = media_internal;
namespace source_clients = media_streams_internal;
别名 source_state、source_clients 实际从未在 media_streams.cpp 之外出现，纯属噪音。

二、函数问题
2.1 手动引用计数函数爆炸（前面已指出）
整个项目里 XxxRef/XxxUnref/XxxRefCopy 函数清单：

code
MediaBufferAddRef / MediaBufferRelease
MediaBufferRef::Allocate / Slice / Reset
MediaFlvCachedVideoTagUnref / MediaFlvCachedVideoTagRefCopy
MediaFlvStartUnref
SubscriptionStartUnref / SubscriptionFrameUnref
MediaSegmentRef / MediaSegmentRefUnref / MediaSegmentRefCopy
ParsedFramePayloadUnref
PoolStateRef / PoolStateUnref
EncodedFrameRefCopy / EncodedFrameUnref (历史)
9 类资源都有自己的 Ref/Unref，相互调用时层层传递。这是 C 风格的内存管理，不是 C++。

2.2 大量 if (ptr != nullptr) 防御性检查
libs/ai/src/ai.cpp：6 个方法都查 impl_ != nullptr
libs/http/src/http_session.cpp：pending != nullptr、request_logs != nullptr
libs/media/src/media_streams.cpp：stream != nullptr、sink != nullptr
impl_ 在构造时 new 出来，构造成功就非空。所有这些 nullptr 检查都是噪音。

2.3 函数过长
文件	行数	问题
libs/media/src/media_streams.cpp	772	Impl 类做了所有事：ref/unref、命名转换、流状态、FLV、HLS、MJPEG、订阅、统计、关键帧
libs/device/src/device_impl.cpp	多	DeviceImpl 同时管：pipeline、配置、image strategy、snapshot、overlay、frame dispatch
libs/ai/src/ai_core.cpp	多	AiCore 管：worker pool、alert capture、inference、perimeter filter
libs/http/src/http_session.cpp	多	HttpSession 管：parse、pipeline、streaming attach、timeout generation、media client binding
2.4 同一个类里 6+ 个方法只差一个判断函数（重复代码）
libs/media/src/media_streams.cpp::MediaStreams::Impl：

cpp
bool IsHlsSupported(StreamId s) const {
  std::lock_guard<std::mutex> guard(mutex_);
  const auto* stream = FindStream(s);
  return stream != nullptr && source_state::IsHlsStreamReady(*stream);
}

bool IsFlvSupported(StreamId s) const { ... IsFlvStreamReady ... }
bool IsMjpegSupported(StreamId s) const { ... IsMjpegStreamReady ... }
bool IsStreamAvailable(StreamId s) const { ... state == kRunning ... }
Codec GetStreamCodec(StreamId s) const { ... stream->codec ... }
MediaStreamInfo GetStreamInfo(StreamId s) const { ... BuildMediaStreamInfo(*stream) ... }
MediaHlsPlaylist GetHlsPlaylist(StreamId s) const { ... BuildHlsPlaylist(*stream, ...) ... }
MediaFlvStart GetFlvStart(StreamId s) const { ... BuildFlvStart(*stream) ... }
8 个方法 99% 代码相同，应该用模板/lambda 合并。

2.5 错误处理只返回 bool，丢失上下文
HttpServer::Prepare()、NetEngineImpl::Start()、MediaStreams::Start()、DeviceImpl::Prepare() 全部返回 bool，调用者无法知道失败原因。

正确做法：返回 Result<T, ErrorCode> 或至少在每个失败点写日志。

三、文件问题
3.1 单文件职责堆叠
libs/media/src/media_streams.cpp 772 行，包含：
9 个 ref/unref 函数
4 个命名转换函数
5 个类型转换函数
MediaStreams::Impl 主类（流状态、FLV、HLS、MJPEG、订阅、统计、关键帧）
违反 AGENTS.md 第 118-121 行自己规定的"按功能职责拆分文件"。

3.2 头文件命名不统一
风格	例子
<module>.h	ai.h、auth.h、device.h、http.h、system.h
<module>_xxx.h	http_dependencies.h、http_access.h、http_router.h、network_api.h、time_api.h、upgrade_package.h
<subsystem>.h	core_subsystem.h、device_subsystem.h、media_subsystem.h、protocol_subsystem.h
infra/<x>.h	infra/log.h、infra/fs.h、infra/time.h
http_dependencies.h 与 http.h 同级，但 network_api.h 与 system.h 同级——前者用前缀，后者用后缀。

3.3 app/modules/ 和 app/tools/ 是空目录
code
app/modules/   ← 空
app/tools/     ← 空
按 AGENTS.md "不删除空目录"原则可以保留，但要标注用途，否则像未完成的工作。

3.4 hisi_vendor 在 libs/device/include/hisisdk/ 和 libs/hisi_vendor/include/hisi_vendor/ 都有相关头文件
libs/device/include/hisisdk/hisi_sdk.h —— IHisiSdk 接口定义
libs/hisi_vendor/include/hisi_vendor/mpp_hisi_sdk.h —— MppHisiSdk 实现
接口定义在 device/include/hisisdk/，实现在 hisi_vendor/include/hisi_vendor/，命名空间都是 live_stream::hisisdk。接口和实现分在两个模块，没有"接口拥有者"概念。

3.5 根目录有 重构AI.md 这种中文文件名
code
live_stream/
  重构AI.md     ← 中文文件名
  AGENTS.md
与 docs/refactor/ 下的英文名风格冲突，且没归到 docs/ 下。

四、结构问题
4.1 依赖关系混乱
模块依赖图（基于 include 推断）：

code
app/subsystems/protocol_subsystem.cpp
  → libs/http, libs/rtsp, libs/webrtc, libs/onvif
  → libs/media (via MediaRefs)
  → libs/device (via DeviceRefs)
  → libs/event, libs/config (via CoreSubsystem)
  → libs/net (direct)

app/subsystems/media_subsystem.cpp
  → libs/device, libs/media, libs/ai

app/subsystems/device_subsystem.cpp
  → libs/device, libs/hisi_vendor (via hisisdk::MppSdk)

libs/http
  → libs/media (依赖 MediaStreams*)
  → libs/device (依赖 DeviceMedia*)
  → libs/rtsp, libs/webrtc, libs/onvif (反向依赖!)

libs/media
  → libs/media_codec, libs/event, libs/infra

libs/device
  → libs/hisi_vendor (via hisisdk::MppSdk)
  → libs/media (via frame_sink.h, media_buffer.h)
关键问题：libs/http 同时依赖 libs/rtsp、libs/webrtc、libs/onvif，这是下层依赖上层——按模块分层，HTTP 协议应该和 RTSP/WebRTC/ONVIF 平级，不应该互相依赖。

4.2 MediaStreams 单类承担太多职责（前面已指出）
MediaStreams 一个类同时做：

接收 MediaFrame
时间戳修正
NAL 解析
FLV 打包
HLS 分片
MJPEG 缓存
GOP 缓存
客户端订阅
关键帧请求
状态查询
统计
没有 Pipeline Stage 抽象，导致所有逻辑耦合在 Impl 里。

4.3 DeviceMedia 和 MediaStreams 之间职责不清
DeviceMedia：硬件层、抓图、Region Overlay
MediaStreams：流分发、FLV/HLS/MJPEG
但 MediaStreams 持有 FrameSink，DeviceMedia 通过 FrameSink 把帧推给 MediaStreams。MediaStreams 又通过 RequestKeyframeFn 回调请求 DeviceMedia 出 IDR。

两个类互相持有对方的回调，强耦合。应该有一个明确的 IFrameSource / IFrameSink / IKeyframeRequester 接口在更下层定义。

4.4 HttpDependencies 是上帝 struct
cpp
struct HttpDependencies {
    INetEngine *net_engine = nullptr;
    event::Loop *net_loop = nullptr;
    IAuth *auth = nullptr;
    ILogger *logger = nullptr;
    IConfig *config = nullptr;
    INetwork *network = nullptr;
    ITime *time = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
    ISystem *system = nullptr;
    IRtsp *rtsp = nullptr;
    OnvifServer *onvif = nullptr;
    IAiView *ai = nullptr;
    DeviceMedia *device = nullptr;
    IWebrtc *webrtc = nullptr;
    MediaStreams *media_streams = nullptr;
    event::Dispatcher *event = nullptr;
};
16 个指针。HTTP 层依赖了整个系统的所有业务模块。这意味着任何模块改 public API，HTTP 都要重编译。

正确做法：HTTP 只依赖 IAuth、IConfig 和必要的 IFrameSource，其他通过事件或 lazy lookup。

4.5 SystemStatusSources 又是上帝 struct
cpp
struct SystemStatusSources {
    ILogger *logger = nullptr;
    IConfig *config = nullptr;
    IAuth *auth = nullptr;
    ITime *time = nullptr;
    INetwork *network = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
    IRtsp *rtsp = nullptr;
    OnvifServer *onvif = nullptr;
    DeviceMedia *device = nullptr;
    IAiView *ai = nullptr;
    IWebrtc *webrtc = nullptr;
    MediaStreams *media_streams = nullptr;
};
13 个指针，和 HttpDependencies 高度重叠。这种"把所有依赖打包"的反模式在项目中至少出现 3 处（HttpDependencies、HttpHandlerDependencies、SystemStatusSources）。

五、框架问题
5.1 没有统一的 Lifecycle 框架
每个模块自己定义 Start()/Stop()，但语义不一致：

模块	状态机
rtsp.cpp:27-33	kCreated → kInitialized → kStarted → kStopped → kDeinitialized 5 态
device_impl.cpp:28-36	kCreated → kInitialized → kStarted → kStopping → kStopped → kDeinitialized → kFailed 7 态
ai_core.cpp	没有显式状态机
media_streams.cpp:197	只有 started_ 一个 bool
http.cpp	只有 initialized_ + started_ 两个 bool
snapshot_capture.cpp:16-22	又是 5 态
5 种不同的状态机定义，没有统一基类。应该有 IService 接口定义统一生命周期：

cpp
class IService {
 public:
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual ServiceState GetState() const = 0;
};
5.2 没有统一的 Event Bus / Signal 框架
libs/event/ 有 Dispatcher、Loop、Executor、Subscription，但没有跨模块事件总线。

模块间通信靠：

IFrameSink::PushFrame() —— 同步回调
RequestKeyframeFn —— 函数指针
event::Dispatcher::Subscribe() —— 但只用于 EventType 枚举的固定事件
ConfigService::Attach() —— 配置变更回调
4 种不同的回调机制，没有统一的事件订阅模型。

5.3 没有 Metrics / 监控抽象
各模块各自定义 Stats struct：

MediaStreamStats、NetStats、RtspServiceStats、WebrtcStats、SystemStatus、MediaBufferPoolStats、EventCounts、AiStats
每个都不同结构，HTTP 接口各自手写 JSON 序列化。没有统一的 Metric 接口，无法做聚合查询。

5.4 配置系统没有 Schema 校验
libs/config/（基于 event.h 推断）有 IConfig::Get(name)、IConfig::Set(name, value, issue)，但配置 schema 散落在各模块的 VerifyXxxConfig 函数里：

device_impl.cpp::VerifyVideoConfig
device_impl.cpp::VerifyImageConfigScope
protocol_subsystem.cpp::VerifyProtocolConfigUpdate
ai_core.cpp 内部校验
没有统一的 JSON Schema 校验，每个模块自己写校验逻辑。

5.5 没有统一的 Resource Budget
MediaStreams 内部缓存：frame_ring_ (128 帧)、flv_live_ring_ (8 client × N 帧)、mjpeg_clients_ (8 client × 1 帧)、hls segments (depth=3 + retain=6)

NetEngine 内部：每个 connection 的 send queue（无上限）

FrameRing::StreamCache::frames：固定 kMaxCachedGopFrames = 128

各模块各自管各自的内存预算，没有全局 ResourceBudget 协调。OOM 风险。

六、设计问题
6.1 MediaBuffer 用 __sync_add_and_fetch 而不是 std::atomic
cpp
// libs/media/src/media_buffer.cpp:35
(void)__sync_add_and_fetch(&buffer->ref_count, 1);
__sync_* 是 GCC 旧式原子操作，C++17 应该用 std::atomic<uint32_t>。但 MediaBuffer 是 POD struct，所以用了 __sync_* 绕过类型系统。

正确做法：把 ref_count 改成 mutable std::atomic<uint32_t>，或者用 intrusive_ptr 模板。

6.2 MediaBuffer 用 malloc/free 而不是 new/delete
cpp
// libs/media/src/media_buffer.cpp:15-26
MediaBuffer* buffer = static_cast<MediaBuffer*>(std::malloc(sizeof(MediaBuffer)));
// ...
std::free(buffer);
C++ 项目里混用 malloc/free 和 new/delete，且没有 RAII 包装。如果 malloc 后构造失败，需要手动 free——容易漏。

6.3 PoolState 又是手动 ref 计数
cpp
// libs/media/src/media_buffer_pool.cpp:33
uint32_t ref_count = 1;

void PoolStateRef(PoolState* state) {
    (void)__sync_add_and_fetch(&state->ref_count, 1);
}

void PoolStateUnref(PoolState* state) {
    if (state == nullptr) return;
    if (__sync_sub_and_fetch(&state->ref_count, 1) != 0) return;
    delete state;
}
又是 __sync_* + 手动管理。整个项目的 MediaBuffer、PoolState、EncodedFrame 都是这种模式，三套独立的引用计数系统并存。

6.4 FlvVideoTagView 内嵌固定大小数组
cpp
// libs/media/src/flv_muxer.h:27-33
struct FlvVideoTagView {
    FlvVideoTagSlice slices[kMaxFlvVideoTagSlices];  // kMaxFlvVideoTagSlices = 130
    size_t slice_count = 0;
    size_t total_size = 0;
    uint32_t timestamp_ms = 0;
    uint8_t header[24] = {};
    uint8_t nal_lengths[kMaxFlvVideoTagSlices][4] = {};  // 520 字节
    uint8_t previous_tag_size[4] = {};
    // ...
};
130 * 4 = 520 字节的 nal_lengths 数组，加上 slices 数组，单个 FlvVideoTagView 接近 2KB。这个 struct 在调用栈上频繁拷贝（FlvMuxer::BuildVideoTagView 返回值、PackagedFrameResult 成员），是性能隐患。

应该用 std::array<FlvVideoTagSlice, kMaxFlvVideoTagSlices> + std::array<uint8_t, kMaxFlvVideoTagSlices * 4>，或者直接用 std::vector 但复用容量。

6.5 FrameRing 用 std::map<Id, State> 而不是 unordered_map
cpp
// libs/media/src/frame_ring.h:123
std::map<FrameSubscriptionId, SubscriptionState> subscriptions_;
FrameSubscriptionId 是 uint64_t，O(log n) 查找。订阅数最多 8 个，影响小，但语义上 unordered_map 更合适。

6.6 StreamContext 内嵌 std::string vps/sps/pps/sequence_header_tag
cpp
// libs/media/src/media_stream_state.h:23-38
struct StreamContext {
    Codec codec = Codec::kH264;
    MediaStreamState state = MediaStreamState::kClosed;
    std::string vps;
    std::string sps;
    std::string pps;
    std::string sequence_header_tag;
    GopCache flv_gop_cache;
    HlsMaker hls_maker;
    // ...
};
主码流和子码流各一个 StreamContext，每个有 4 个 std::string。SPS/PPS/VPS 一般几十到几百字节，但 std::string 有 SSO 优化。问题是 sequence_header_tag 可能很大（包含 SPS+PPS+FLV header），每次 ResetStream 都要 clear()。

6.7 JpegFrame/YuvFrame/SnapshotFrame 三个 struct 几乎相同
cpp
// libs/device/include/hisisdk/hisi_sdk.h:75-133
struct JpegFrame {
    MediaBuffer* buffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts_us = 0;
    // 完整的 copy/move/destruct 实现
};

struct YuvFrame {
    MediaBuffer* buffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride_y = 0;
    uint32_t stride_uv = 0;
    int64_t pts_us = 0;
    MppYuvFrameInfo mpp_info;
    // 完整的 copy/move/destruct 实现
};

// libs/device/include/device.h:48-80
struct SnapshotFrame {
    MediaBuffer *buffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts_us = 0;
    // 又是完整的 copy/move/destruct 实现
};
三个 struct 95% 字段相同，各自重写一遍 copy/move/destruct。应该用模板或基类：

cpp
template <typename Extra>
struct FrameBase {
    MediaBufferRef buffer;
    uint32_t offset = 0, size = 0, width = 0, height = 0;
    int64_t pts_us = 0;
    Extra extra;
};

struct JpegExtra {};
struct YuvExtra { uint32_t stride_y, stride_uv; MppYuvFrameInfo mpp_info; };

using JpegFrame = FrameBase<JpegExtra>;
using YuvFrame = FrameBase<YuvExtra>;
6.8 MppHisiSdkImpl 用 pthread_t 和 std::thread 混用
cpp
// libs/hisi_vendor/src/mpp_hisi_sdk_impl.h:42-44
pthread_t isp_thread_ = 0;
std::thread stream_thread_;
pthread_t 是 C 接口，std::thread 是 C++ 接口。同一项目同一类内混用两个线程库，风格不统一。

6.9 MppHisiSdkImpl 用 std::recursive_mutex 而其他模块用 std::mutex
cpp
// libs/hisi_vendor/src/mpp_hisi_sdk_impl.h:46
std::recursive_mutex control_mutex_;
recursive_mutex 通常是设计缺陷的信号——说明调用链可能在持锁时再次调用自己。其他模块都用 std::mutex，只有 MppHisiSdk 用 recursive，说明它的调用结构没理清。

6.10 全局单例泛滥
cpp
// app/application/application.cpp:86-89
Application &Application::Get() {
    static Application application;
    return application;
}

// app/subsystems/core_subsystem.h:16
static CoreSubsystem& Get();

// app/subsystems/media_subsystem.h:23
static MediaSubsystem& Get();

// app/subsystems/protocol_subsystem.h:32
static ProtocolSubsystem &Get();

// app/subsystems/device_subsystem.h:类似
static DeviceSubsystem &Get();

// libs/hisi_vendor/include/hisi_vendor/mpp_hisi_sdk.h:65
IHisiSdk& MppSdk();

// libs/device/src/stub_hisi_sdk.h
IHisiSdk& DefaultSdk();
至少 7 个全局单例。AGENTS.md 第 95 行明确说"业务 service、net、media、auth 不做全局单例或 ServiceLocator"——但 Application::Get() 和 XxxSubsystem::Get() 就是全局单例。

6.11 EncodedFrameCallback 用裸函数指针 + void* user
cpp
// libs/media/include/media/frame_sink.h:26
using EncodedFrameCallback = void (*)(const MediaFrame &frame, void *user);
C 风格回调，类型不安全。std::function<void(const MediaFrame&)> 更现代，但开销稍大。考虑到嵌入式场景，可以用 std::function + 小对象优化，或者定义一个 IFrameCallback 接口。

6.12 HTTP Handler 用静态方法 + void* user 注册
cpp
// libs/http/src/handlers/config_handler.cpp:42-45
static HttpResponse HandleConfigRoute(void *user, const HttpRequest &request) {
    return static_cast<ConfigHttpHandler *>(user)->HandleConfig(request);
}

router->AddPrefixRoute(HttpMethod::kGet, "/api/config/",
                       &ConfigHttpHandler::HandleConfigRoute, this);
每个 handler 都要写一个静态 thunk 函数把 void* 转回 this。C++ 应该用 std::function 或 std::bind，或者用模板：

cpp
template <typename T>
void AddRoute(HttpMethod m, const char* path, 
              HttpResponse (T::*handler)(const HttpRequest&), T* obj);
6.13 INFRA_LOG_* 宏全局污染命名空间
cpp
// libs/infra/include/infra/log.h:96-107
#define Trace(module, fmt, ...) ...
#define Debug(module, fmt, ...) ...
#define Info(module, fmt, ...) ...
#define Warn(module, fmt, ...) ...
#define Error(module, fmt, ...) ...
#define Fatal(module, fmt, ...) ...
Trace、Debug、Info、Warn、Error、Fatal 都是常见标识符，全局宏定义会与用户代码冲突。应该加前缀：INFRA_LOG_INFO、INFRA_LOG_ERROR 等。

七、并发问题
7.1 MediaStreams::Impl 用一把大锁
cpp
// libs/media/src/media_streams.cpp
mutable std::mutex mutex_;  // 保护所有状态
PushFrame、SetStreamState、IsHlsSupported、IsFlvSupported、IsMjpegSupported、GetHlsPlaylist、GetFlvStart、AttachFlvClient、DetachFlvClient、SubscribeFrames、PopSubscriptionFrame...所有方法都抢同一把锁。

主码流和子码流之间没有锁分离，FLV client 和 HLS client 之间没有锁分离。写多读少的场景下，所有消费者被串行化。

正确做法：每路流一把锁，或者用 shared_mutex 区分读写。

7.2 HttpSession 的 timeout_generation_ 没有原子保护
cpp
// libs/http/src/http_session.h
uint64_t timeout_generation_ = 0;  // 非 atomic
但 AppendRequestBytes（IO 线程）和 InstallTimeout/CancelTimeout（Timer 线程）都可能读写它。如果是单线程 IO 模型则没问题，但代码里没注明。

7.3 DeviceImpl 用两把锁但语义不清
cpp
// libs/device/src/device_impl.cpp:160-161
mutable std::mutex mutex_;
std::mutex pipeline_op_mutex_;
mutex_ 保护状态，pipeline_op_mutex_ 保护 pipeline 操作。但调用链上两者可能嵌套，没说明加锁顺序，死锁风险。

八、内存问题
8.1 std::vector<uint8_t> 在热路径堆分配
cpp
// libs/media/src/media_buffer.cpp:100
uint8_t* data = static_cast<uint8_t*>(std::malloc(capacity));
每帧一次 malloc/free，碎片化严重。前面已经讨论过。

8.2 std::string 在热路径频繁构造
FlvMuxer::BuildSequenceHeader 返回 std::string
HlsMaker 内部用 std::string 累积分片
BuildHlsPlaylist 返回包含 std::vector<MediaHlsEntry> 的 MediaHlsPlaylist
每帧打包时多次 std::string 构造/析构，热路径上的堆分配。

8.3 FlvVideoTagView 在栈上分配 2KB
前面 6.4 已指出，每次 BuildVideoTagView 在栈上分配 ~2KB 的 view，然后 PackagedFrameResult 又复制一份。

8.4 closed_connections_ 用 unordered_map + deque 做诊断历史
cpp
// libs/net/src/net_engine_impl.h:107-109
std::unordered_map<ConnectionId, NetConnectionInfo> closed_connections_;
std::deque<ConnectionId> closed_connection_order_;
诊断用的连接历史，没有上限会一直增长。需要 LRU 或固定大小。

8.5 frame_ring.h::StreamCache::frames 是 std::array<CachedFrame, 128>
cpp
// libs/media/src/frame_ring.h:59
std::array<CachedFrame, kMaxCachedGopFrames> frames;  // 128 个 CachedFrame
每个 CachedFrame 包含 FramePayload payload，FramePayload 又包含 MediaFrame，MediaFrame 又包含 MediaBufferRef。

主+子两路流，每路 128 帧 = 256 个 CachedFrame 常驻栈/堆。即使空闲也占内存。

九、接口契约问题
9.1 MediaFrame 和 EncodedFrame 命名混淆
历史代码里可能有 EncodedFrame，现在统一改成 MediaFrame，但：

EncodedFrameCallback 这个 typedef 还在用（frame_sink.h:26）
MediaFrame 实际就是编码后的帧（H.264/H.265 NAL）
名字 MediaFrame 不如 EncodedFrame 准确——"Media" 比 "Encoded" 含义更模糊。

9.2 IHisiSdk 接口方法过多（违反 ISP）
cpp
// libs/device/include/hisisdk/hisi_sdk.h:252-300
class IHisiSdk {
    virtual MediaCapabilities GetCapabilities() = 0;
    virtual bool InitSystem(...) = 0;
    virtual bool DeinitSystem() = 0;
    virtual bool StartVi(...) = 0;
    virtual void StopVi(...) = 0;
    virtual bool StartVpss(...) = 0;
    virtual void StopVpss(...) = 0;
    virtual bool BindViVpss(...) = 0;
    virtual void UnbindViVpss(...) = 0;
    virtual bool StartVenc(...) = 0;
    virtual void StopVenc(...) = 0;
    virtual bool BindVpssVenc(...) = 0;
    virtual void UnbindVpssVenc(...) = 0;
    virtual bool StartVencStream(...) = 0;
    virtual void StopVencStream(...) = 0;
    virtual bool RequestIdr(...) = 0;
    virtual bool ApplyVencRoi(...) = 0;
    virtual bool ApplyImageConfig(...) = 0;
    virtual ExposureInfo QueryExposureInfo(...) = 0;
    virtual bool CreateRegion(...) = 0;
    virtual bool AttachRegion(...) = 0;
    virtual bool DetachRegion(...) = 0;
    virtual bool SetRegionDisplay(...) = 0;
    virtual bool SetRegionBitmap(...) = 0;
    virtual void DestroyRegion(...) = 0;
    virtual JpegFrame CaptureJpeg(...) = 0;
    virtual YuvFrame CaptureYuvFrame(...) = 0;
};
26 个虚方法，混合了 5 类职责：系统初始化、VI/VPSS/VENC 流水线、码流回调、Region OSD、Snapshot。

应该按职责拆分：

cpp
class IHisiSystem { InitSystem/DeinitSystem };
class IHisiPipeline { StartVi/StopVi/StartVpss/.../BindViVpss/... };
class IHisiVencStream { StartVencStream/StopVencStream/RequestIdr };
class IHisiRegion { CreateRegion/AttachRegion/... };
class IHisiSnapshot { CaptureJpeg/CaptureYuvFrame };
class IHisiImage { ApplyImageConfig/ApplyVencRoi/QueryExposureInfo };
调用方按需依赖，不依赖未用方法。

9.3 IFrameSink::PushFrame 返回 bool 但语义不清
cpp
class FrameSink {
public:
    virtual bool PushFrame(const MediaFrame &frame) = 0;
};
返回 bool 表示什么？接受？成功？继续？没有文档说明。如果返回 false，上游怎么办？丢帧？重试？关闭？

9.4 IMediaFlvSink::OnFlvChunk 和 OnFlvVideoTag 两个方法职责重叠
cpp
class IMediaFlvSink {
public:
    virtual bool OnFlvChunk(const uint8_t *data, size_t size) = 0;
    virtual bool OnFlvVideoTag(const MediaFlvVideoTagView &tag,
                               const MediaFrame &frame) = 0;
};
OnFlvChunk 接收已序列化的字节流，OnFlvVideoTag 接收 view 形式。两个方法都是 FLV 数据，调用方需要决定用哪个——但接口没有说明何时调用哪个。

十、其他细节
10.1 版权头文件污染
cpp
// libs/system/include/system.h:1-6
/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: system.h
 * Brief: Defines the IPC system management public API.
 */
只有部分文件有版权头（system.h、auth.h、log.h、logger.h），其他文件没有。版权信息应该统一要么全有要么全无。

10.2 中英文注释混用
media_stream_state.h:21：中文注释
frame_ring.h:44：中文注释
flv_muxer.h:19：中文注释
tcp_session.h:91：中文注释
media_streams.h:35：英文注释
http_session.h:17：英文注释
同一个项目内中英文混用，没有统一规则。

10.3 using namespace 在头文件
cpp
// libs/ai/src/ai_core.cpp:43-48
using ai_internal::AiBackendToString;
using ai_internal::AiInferenceEngine;
using ai_internal::CreateAiEngine;
using ai_internal::FilterPerimeterDetections;
using ai_internal::IsValidAiConfig;
using ai_internal::ParseAiConfig;
.cpp 文件里的 using，可以接受。但是 6 个 using 表明 ai_internal 命名空间不必要——如果直接放到 live_stream 命名空间下，代码更简洁。

10.4 static_cast<ThisClass *>(user) 模式重复
每个 HTTP handler 都有：

cpp
static HttpResponse HandleXxxRoute(void *user, const HttpRequest &request) {
    return static_cast<XxxHttpHandler *>(user)->HandleXxx(request);
}
13 个 handler × 1 个 thunk = 13 处重复代码。应该用模板或 CRTP 消除。

10.5 MakeXxxHandler 工厂函数 13 个
cpp
// libs/http/src/handlers/http_handlers.h:81-108
MakeAuthHandler(access, auth);
MakeConfigHandler(access, config);
MakeOperationsHandler(access, logger);
MakeNetworkHandler(access, network);
MakeTimeHandler(access, time);
MakeUpgradeHandler(access, upgrade);
MakeSystemHandler(access, system, status_sources);
MakeAlarmHandler(access, alarm);
MakeMediaHandler(access, config, device, media_streams, rtsp, webrtc, http);
MakeAiHandler(access, config, ai, device);
MakeSnapshotHandler(access, device);
MakeEventStreamHandler(access);
13 个工厂函数签名各不相同。应该统一为：

cpp
std::unique_ptr<IHttpHandler> CreateHandler(
    HttpHandlerKind kind, 
    const HttpHandlerDependencies& deps);
http_handler_factory.cpp 已经实现了这个统一工厂，但每个 MakeXxxHandler 还在暴露给外部——两套 API 并存。

10.6 app/application/application.cpp 的 HandleSegv 直接 _exit
cpp
// app/application/application.cpp:69
_exit(128 + sig);
崩溃后直接退出，不调用析构、不刷新日志缓冲区、不写 minidump。生产环境应该至少 flush 日志、写崩溃报告。

10.7 g_stop_requested 是 volatile sig_atomic_t 但用 == 比较
cpp
// app/application/application.cpp:23
volatile std::sig_atomic_t g_stop_requested = 0;

// :169
while (g_stop_requested == 0) { ... }
volatile 在 C++ 中不保证内存可见性，应该用 std::atomic<bool>。sig_atomic_t 在 C 中是保证信号安全的，但 C++ 多线程下应该用 atomic。

10.8 Application::Stop() 顺序硬编码
cpp
// app/application/application.cpp:152-165
void Application::Stop() {
    ProtocolSubsystem::Get().Stop();
    MediaSubsystem::Get().Stop();
    DeviceSubsystem::Get().Stop();
    CoreSubsystem::Get().Stop();
}
停止顺序写死，新增 subsystem 必须改这里。应该用 vector<ISubsystem*> 按反序 stop。

汇总表
按严重程度分级：

级别	问题数	典型问题
P0 严重	8	手动 ref 计数爆炸、MediaStreams 单类承担 11 职责、HttpDependencies 上帝 struct、IHisiSdk 26 方法违反 ISP、全局单例违反 AGENTS、模块反向依赖、热路径堆分配、一把大锁串行化
P1 重要	12	命名维度混乱、logger vs log 混淆、状态机 5 套不统一、缺 ResourceBudget、缺统一 Metric、__sync_* 代替 atomic、malloc/free 代替 new/delete、FlvVideoTagView 栈上 2KB、volatile 不保证可见性、_exit 不 flush、中英文注释混用、HTTP handler 13 处重复 thunk
P2 改进	15	namespace 别名噪音、过度防御性 nullptr 检查、单文件 772 行、6 个相同 lock_guard 方法、bool 失败无上下文、JpegFrame/YuvFrame/SnapshotFrame 95% 重复、pthread_t 与 std::thread 混用、recursive_mutex 设计缺陷、std::map 应改 unordered_map、closed_connections 无上限、FlvVideoTagView 内嵌大数组、EncodedFrameCallback 裸函数指针、HTTP handler 13 个工厂函数、版权头不统一、g_stop_requested 用 volatile
P3 风格	10+	internal namespace 命名不一致、类名后缀 6 种混用、头文件命名前缀/后缀混用、app/modules/ app/tools/ 空目录、根目录中文文件名、using 6 个声明、空 .codex 文件等
核心结论
工程骨架（模块划分、依赖注入、PIMPL、Strategy）是产品级的，但实现细节有 45+ 个具体问题，分布在前述 10 个维度。

让人感觉 "low" 的不是单一问题，而是：

手动引用计数 是 C 风格，不是 C++ RAII
上帝类/上帝 struct 让单个类/struct 承担过多职责
5 套状态机 + 7 个全局单例 没有统一框架
接口违反 ISP (IHisiSdk 26 方法)
模块反向依赖 (http 依赖 rtsp/webrtc/onvif)
热路径堆分配 + 一把大锁 性能差
命名维度混乱 让代码看起来"想到什么加什么"
如果你要把这份扫描结果也落到 docs/refactor/question.md，建议在原文件追加"## 六、第二轮全工程深度扫描"章节，把上面的内容粘进去。要不要我帮你整理成可直接复制的 Markdown 内容？