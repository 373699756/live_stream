# media

## 模块定位

`media` 是重构后的通用媒体核心，承载视频帧内存、主/子码流缓存、HLS/FLV/MJPEG
输出数据和协议帧订阅。它不启动设备硬件、不解析 HTTP/RTSP/WebRTC 请求，也不依赖
`device`、`hisi_vendor` 或配置系统。

## 核心职责

- 定义 `Codec`、`MediaBufferRef`、`MediaBufferBuilder`、`MediaFrame` 和
  `MediaOutSlice` 等通用媒体类型。
- 通过 `FrameSink::PushFrame()` 接收设备侧输出的编码帧。
- 维护主/子码流的 GOP cache、HLS segment、FLV 起播缓存、MJPEG latest frame 和
  帧订阅 live queue。
- 通过 `RequestKeyframeFn` 把协议侧新订阅、恢复等关键帧请求回调给设备层；
  `media` 自身不直接依赖设备模块。
- 对协议模块暴露 `MediaStreams`、`MediaStreamInfo`、`MediaStreamStats`、
  `FrameSubscription` 相关接口。
- 帧订阅起播 GOP 和 live frame 直接返回带引用计数 payload 的 `MediaFrame`；
  参数集、codec generation 和 90kHz clock rate 等播放元信息收敛在
  `MediaStreamInfo`。

## 状态与资源模型

`MediaStreams` 拥有码流缓存和订阅队列。输入帧使用 RAII `MediaBufferRef`
保活，协议订阅和 FLV GOP cache 只增加底层 buffer 引用，不复制整帧
payload；HLS segment 是独立转封装后的 TS buffer。

`MediaStreams` 只做协调：`MediaStreamTracks` 持有主/子码流的 codec、参数集、
HLS/FLV/MJPEG 缓存和 reset 规则；`FrameRing` 持有协议帧订阅和 live queue；
`PreviewClients` 持有 HTTP-FLV/MJPEG preview client、sink 生命周期和
pending write 数量。集合当前基数接口使用 `Size()`，不要使用 `Count()`。不要再把
`MediaStreams` 按 start/input/output 这类函数主题
拆文件；只有真实拥有状态、资源或生命周期规则的对象才单独成文件。

codec 切换、stream stop 和 timestamp reset 会清理 GOP、HLS、FLV、MJPEG 和
订阅 live queue，后续从新的关键帧重新建立可播放状态。

`MediaFrame` 是公共编码帧值对象，拷贝只增加 `MediaBuffer` 引用计数。
订阅方、FLV cache、MJPEG latest frame 和 HLS segment ref 都不再暴露手动
`Unref/RefCopy/Move` cleanup API。

`MediaBufferRef` 对外只读，发布后的 payload 只能通过 `Data()`、`Size()` 和
`Slice()` 读取。需要填充或扩容 payload 的代码使用 `MediaBufferBuilder`，完成后
调用 `Finish()` 得到只读 `MediaBufferRef`。手动 `AddRef/Release` 和裸 owner 不进入
public API；HTTP/RTSP 发送队列需要跨线程保活媒体 payload 时，直接把
`MediaBufferRef` 值对象随 `NetBufferSlice` 入队。

`SubscriptionStart::track_ready` 表示订阅起播数据已具备协议输出条件，
不要用设备运行态替代该判断。

`FramePayload`、NAL 解析结果和时间戳修正器只属于 `media` 内部实现，不进入 public
header；设备侧只需要实现 `FrameSink::PushFrame()`。

## 非目标

- 不直接调用 HiSilicon SDK。
- 不拥有设备启动、停止、抓图、图像参数或区域叠加。
- 不拥有 HTTP/RTSP/WebRTC socket、会话或认证状态。
