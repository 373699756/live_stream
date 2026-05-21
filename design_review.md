# live_stream 整体设计审查报告

## 一、模块划分合理性

### ✅ 合理的地方

**分层清晰，职责单一**

```
app/                  — 组合根，负责服务创建、依赖注入、启停顺序
libs/*_service/       — 业务服务模块，各自独立编译为静态库
libs/common/          — 共享数据类型和轻量工具
libs/infra_service/   — 进程级基础设施（日志/文件/时间/Executor）
www/                  — 前端 Web Console，仅通过 HTTP API 消费后端
```

**22 个模块的职责边界清晰：**

| 模块 | 职责 | 评价 |
|------|------|------|
| infra_service | Log/Executor/Fs/Time 基础设施 | ✅ 精简，无业务逻辑 |
| config_service | JSON 配置加载/存储/回调 | ✅ 职责单一 |
| event_service | 进程内 pub/sub | ✅ 轻量 |
| auth_service | 登录/会话/令牌验证 | ✅ 边界清晰 |
| logger_service | 操作日志记录/查询 | ✅ 与 infra::Log 正确分离 |
| system_service | 设备状态查询/重启/恢复出厂 | ✅ 职责明确 |
| network_service | 网络接口配置/状态 | ✅ 平台抽象完善 |
| time_service | NTP/时间配置 | ✅ 平台抽象完善 |
| alarm_service | 报警规则/状态 | ✅ 职责单一 |
| upgrade_service | 固件升级流程 | ✅ 平台抽象完善 |
| media_service | 视频媒体管线 + HiSi SDK 集成 | ✅ 核心模块，职责明确 |
| snapshot_service | 抓图策略/JPEG 获取 | ✅ 职责单一 |
| osd_service | OSD 配置/MPP 区域管理 | ✅ 职责单一 |
| ai_service | AI 推理配置/运行时（可选） | ⚠️ 定位仍模糊（见问题部分） |
| net_service | TCP/UDP 基础网络引擎 | ✅ 无业务语义，仅传输 |
| rtsp_service | RTSP 协议/会话 | ✅ 职责清晰 |
| webrtc_service | WebRTC 信令/媒体传输 | ✅ 职责清晰 |
| stream_hub_service | 多消费者流分发（HLS/FLV/WebRTC） | ✅ 扇出逻辑集中 |
| onvif_service | ONVIF 设备发现/媒体服务 | ✅ 协议封装完整 |
| http_service | HTTP 服务器/路由/认证/静态文件 | ✅ Handler 已领域分组 |
| stream_codec | NAL 单元解析/H264/H265 | ✅ 纯函数库，无状态 |
| stream_mux | RTP 打包/FLV/TS 封装 | ✅ 纯函数库，无状态 |

---

## 二、接口精简合理性

### ✅ 接口设计的优点

**1. IFrameSource / IFrameSink — 精简，正确**
```cpp
class IFrameSource {
    bool IsStreamStarted(StreamId stream_id) const = 0;
    VideoCodec GetStreamCodec(StreamId stream_id) const = 0;
    FrameSubscriptionId SubscribeFrames(...) = 0;
    bool UnsubscribeFrames(FrameSubscriptionId) = 0;
    bool RequestKeyFrame(StreamId, KeyFrameReason) = 0;
};
```
5 个方法，精确覆盖消费者需求。

**2. IMediaView — 正确的窄接口**
```cpp
class IMediaView {
    bool IsStarted() const = 0;
    bool IsRestarting() const = 0;
    bool IsStreamStarted(StreamId) const = 0;
    MediaCapabilities GetCapabilities() const = 0;
    MediaChannels GetChannels() const = 0;
};
```
HttpService、RtspService、OnvifService 通过此接口而非具体类访问 MediaService，Phase 2 已完成。

**3. IHttpHandler / IHttpRouter — 正确的扩展点**
每个业务 handler 是独立的 `IHttpHandler` 子类，通过 `RegisterRoutes(IHttpRouter*)` 注册，可独立测试。

**4. IRtspAdaptiveObserver — 自适应观察者模式**
RTSP 的自适应调整通过回调接口（`OnRtspAdaptiveSample`）反馈，与 RTSP 协议逻辑解耦。

### ❌ 接口问题

**1. MediaService 暴露了具体类而非纯接口（最重要问题）**
```cpp
class MediaService : public IMediaView, public IFrameSource {
public:
    // 公开方法太多（17+），包含内部实现细节：
    bool SetEncodedFrameCallback(EncodedFrameCallback callback, void* user);
    MppChannel GetMainVpssChannel() const;
    MppChannel GetMainVencChannel() const;
    MediaServiceStats GetStats() const;
    static const char* StaticName();
    // ...
};
```
`MediaService` 没有单独的接口类（`IMediaService`），消费方直接持有具体类指针。
`GetMainVpssChannel()` / `GetMainVencChannel()` 属于内部 MPP 配置，不应出现在公共接口里。

**建议：** 将 `MediaService` 拆分为接口 + 实现，公共接口只保留 `IMediaView` + `IFrameSource` 所需方法。

**2. IRtspService::PushFrame() 接口语义混乱**
```cpp
class IRtspService {
    bool PushFrame(const EncodedFrame& frame) = 0;
};
```
RTSP 服务是帧消费者，不应通过推送接口接收帧，应通过 `IStreamHubService` 的订阅机制拉取。
且 `RtspServiceDependencies` 中有 `stream_hub` 字段，说明已有订阅机制，`PushFrame` 是多余的旧接口。

**3. system_handler.cpp include 了大量具体类头文件**
```cpp
#include "alarm_service.h"
#include "media_service.h"
#include "network_service.h"
#include "rtsp_service.h"
// ... 共 10 个具体类头文件
```
Handler 层字段类型都是接口指针，这些具体类头文件大多不必要，增加编译依赖。

---

## 三、性能合理性

### ✅ 性能设计正确的地方

**1. 4 个 Executor 分流（HTTP 层）**
```
config_apply_executor  — 配置写入（串行，避免冲突）
control_executor       — 控制命令（低并发）
stream_executor        — 流媒体请求（专用线程）
task_executor          — 普通 API（4 worker）
```
配置变更不会阻塞流媒体，流媒体不会阻塞普通 API。

**2. IFrameSink 零拷贝设计**
`IStreamHubService` 的 FLV 客户端接口传递 `const uint8_t*` 而非 `std::string`，避免帧数据复制。

**3. RtpPacketView 零拷贝 scatter-gather**
```cpp
struct RtpPacketView {
    RtpPacketSlice slices[kMaxRtpPacketSlices];  // 最多 4 个 slice
};
```
RTP 包通过 slice 列表描述，不合并内存，避免 header + payload 的拼接拷贝。

**4. MediaBuffer 内存池（推测）**
`media_buffer.cpp` / `media_buffer_pool.cpp` 的存在说明媒体帧有内存池管理，避免频繁分配。

**5. NetEngine 多 IO 线程 + epoll**
`EventLoop` 基于 epoll，NetEngine 支持配置多 IO 线程，对高并发 RTSP 会话有扩展能力。

### ⚠️ 性能隐患

**1. `GetStats()` 在 HandleStatus 中被多次调用**
```cpp
// system_handler.cpp 中连续两次调用 webrtc_service->GetStats()
dependencies_.webrtc_service->GetStats().enabled &&
dependencies_.webrtc_service->GetStats().backend_available
```
`GetStats()` 通常加锁，连续调用两次导致两次锁竞争。应缓存结果：
```cpp
const auto stats = dependencies_.webrtc_service->GetStats();
add_service("webrtc_service", stats.enabled && stats.backend_available);
```

**2. RtpPacketizer 按帧分配内存**
`Packetize()` 内部逐帧处理，若使用 `std::string` 作为中间 buffer 会有频繁分配。需确认实现是否使用预分配 buffer。

**3. RTSP 发送队列限制合理性**
```cpp
uint32_t send_queue_capacity = 128;
uint32_t send_buffer_limit_bytes = 1024 * 1024;  // 1MB
```
1MB 对于 2~4Mbps 视频流约 0.5 秒缓冲，对慢速客户端可能太小，导致帧丢弃。可根据实际码率动态评估。

---

## 四、功能划分问题

**1. `stream_hub_service` 承担了过多角色**
当前 `IStreamHubService` 同时负责：
- HLS 分段生成/存储
- FLV 流分发（多客户端管理）
- 帧订阅（`AttachFrameSink`）
- 关键帧请求转发

建议考虑将 HLS 分段逻辑和 FLV 分发逻辑分离，但嵌入式环境下合并也可接受，不是硬性问题。

**2. `MediaService` 直接暴露 `GetMainVpssChannel()` 到公共接口**
这是 HiSilicon MPP 内部通道号，属于硬件实现细节，不应出现在面向外部消费者的接口中。仅应在 `MediaSubsystem` 组装时内部使用（传给 SnapshotService 等）。

**3. `RtspServiceDependencies` 缺少 `media_service`**
```cpp
struct RtspServiceDependencies {
    NetEngine* net_engine = nullptr;
    IAuthService* auth_service = nullptr;
    IEventService* event_service = nullptr;
    IStreamHubService* stream_hub = nullptr;
    IRtspAdaptiveObserver* adaptive_observer = nullptr;
    // 没有 media_service！
};
```
RTSP 通过 `stream_hub` 订阅帧，设计是正确的。但 `IRtspService::PushFrame()` 还存在于接口中（见上），需清理。

---

## 五、改进优先级

| 优先级 | 问题 | 改动量 | 影响 |
|--------|------|--------|------|
| P1 | `GetStats()` 重复调用（system_handler.cpp） | 极小 | 运行时性能 |
| P1 | 清理 system_handler.cpp 中多余的具体类 `#include` | 小 | 编译依赖/编译速度 |
| P2 | 移除 `IRtspService::PushFrame()`（已有 stream_hub 机制） | 小 | 接口清洁度 |
| P2 | `MediaService` 引入纯接口，隐藏 MPP 内部方法 | 中 | 可测试性/接口清洁度 |
| P3 | `stream_hub_service` HLS/FLV 职责拆分（可选） | 大 | 架构清晰度 |

---

## 六、总体结论

**整体设计合理，已处于较高质量水平。** 主要优点：

- 依赖方向单向，无循环依赖
- 22 个模块职责边界清晰
- 接口抽象（IMediaView/IFrameSource/IHttpHandler）已落地
- 零拷贝传输路径设计正确
- 4 Executor 分流机制适合嵌入式资源受限场景

**需要重点修复的问题只有 3 个：**

1. `system_handler.cpp` 中 `GetStats()` 重复调用（一行修复）
2. `system_handler.cpp` 中多余的具体类头文件 include（10 行修改）
3. `IRtspService::PushFrame()` 残留旧接口（需确认是否仍被调用）
