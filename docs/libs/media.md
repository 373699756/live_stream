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
- 通过 `MediaSourceRegistry` 暴露当前 `MediaStreams` 入口；组合根负责在
  `MediaStreams` 启动成功后注册、停止前清理，协议模块不再通过自身
  构造参数保存 app 传入的媒体源字段。
- 帧订阅起播 GOP 和 live frame 直接返回带引用计数 payload 的 `MediaFrame`；
  参数集、codec generation 和 90kHz clock rate 等播放元信息收敛在
  `MediaStreamInfo`。

## 状态与资源模型

`MediaStreams` 拥有码流缓存和帧客户端共享缓存。输入帧使用 RAII `MediaBufferRef`
保活，协议订阅和 FLV GOP cache 只增加底层 buffer 引用，不复制整帧
payload；HLS segment 是独立转封装后的 TS buffer。
`MediaSourceRegistry` 只保存 non-owning `MediaStreams*`，不代理帧操作、
不控制媒体生命周期，也不新增缓存。它的作用是消除协议构造 DTO 对
`MediaStreams*` 的直接字段依赖。

`MediaStreamsOptions::cache_limits` 定义 media 自己拥有的缓存运行期上限：
subscription 起播 GOP frame/bytes、共享 live frame/bytes、
HTTP-FLV cached tag/bytes、HLS segment 数量、单 segment bytes 和 HLS cached bytes。
这些上限只约束 `media` 内部缓存，不包含 FLV/MJPEG client 数、frame subscription
数、socket send queue 或 WebRTC peer 数。内部 `FrameSubscribers`、`GopCache` 和
`HlsMaker` 仍使用固定数组或明确 owner 的 buffer 作为硬边界，运行期上限只控制可用窗口
和累计字节，避免热路径按帧分配策略对象。

`MediaStreamStats` 暴露当前缓存和触顶事件：`cached_bytes` 是主/子码流 GOP、共享
live frame、HLS segment 和 FLV GOP cache 的总量近似值；`main_*`、`sub_*` 字段分别给出 GOP/HLS/FLV
字节和 cache drop、client frame drop 计数。慢 subscription 落后到共享帧缓存覆盖边界后会
等待下一个关键帧恢复，避免从 P/B 帧继续输出。

`MediaStreams` 只做协调：`MediaStreamTracks` 持有主/子码流的 codec、参数集、
HLS/FLV/MJPEG 缓存和 reset 规则；`FrameSubscribers` 持有协议帧订阅和共享 live frame；
`PreviewClients` 持有 HTTP-FLV/MJPEG preview client、sink 生命周期和
pending write 数量。集合当前基数接口使用 `Size()`，不要使用 `Count()`。不要再把
`MediaStreams` 按 start/input/output 这类函数主题
拆文件；只有真实拥有状态、资源或生命周期规则的对象才单独成文件。

`FrameSubscribers` 内的共享 live frame 使用 `event::MultiReaderQueue` 作为固定容量
多读者缓存：生产者每帧只写一次，RTSP/WebRTC 客户端只保存自己的读取位置。慢客户端
读到已覆盖边界时只标记该客户端 `wait_keyframe`，不会清空其它客户端状态。

codec 切换、stream stop 和 timestamp reset 会清理 GOP、HLS、FLV、MJPEG 和
共享 live frame，后续从新的关键帧重新建立可播放状态。

`MediaFrame` 是公共编码帧值对象，拷贝只增加 `MediaBuffer` 引用计数。
订阅方、FLV cache、MJPEG latest frame 和 HLS segment ref 都不再暴露手动
`Unref/RefCopy/Move` cleanup API。

`MediaBufferRef` 对外只读，发布后的 payload 只能通过 `Data()`、`Size()` 和
`Slice()` 读取。需要填充或扩容 payload 的代码使用 `MediaBufferBuilder`，完成后
调用 `Finish()` 得到只读 `MediaBufferRef`。手动 `AddRef/Release` 和裸 owner 不进入
public API；HTTP/RTSP 发送队列需要跨线程保活媒体 payload 时，直接把
`MediaBufferRef` 值对象随 `SocketWriteSlice` 入队。

`SubscriptionStart::track_ready` 表示订阅起播数据已具备协议输出条件，
不要用设备运行态替代该判断。

`FramePayload`、NAL 解析结果和时间戳修正器只属于 `media` 内部实现，不进入 public
header；设备侧只需要实现 `FrameSink::PushFrame()`。

## 参考项目检查点

`my_video` 中固定帧队列、旧帧覆盖和 H.264 AnnexB 解析的经验说明，媒体热路径必须在
核心层统一处理资源边界，而不是让每个协议各自兜底。`media` 后续优化按以下口径执行：

- VENC payload 进入 `MediaBufferRef` 后，协议模块只能共享引用和 slice；除 HLS segment
  自包含输出、WebRTC SRTP 加密等明确边界外，不新增整帧深拷贝。
- GOP、subscription start、shared live frame、HLS segment、FLV cache 和 MJPEG latest
  frame 都必须有 frame/bytes 上限；新增缓存前先更新本模块文档。
- 慢 subscription 读到覆盖边界时只影响该订阅并等待下一个关键帧，不清空其它客户端状态。
- codec 切换、stream stop、timestamp reset 或参数集变化必须一次性清理所有可播放缓存，
  后续从新的关键帧和参数集重新建立 ready。
- H.264/H.265 参数集提取只用于低频 metadata 输出，热路径不得为每个协议包重复解析并复制
  大 payload。

## 非目标

- 不直接调用 HiSilicon SDK。
- 不拥有设备启动、停止、抓图、图像参数或区域叠加。
- 不拥有 HTTP/RTSP/WebRTC socket、会话或认证状态。
