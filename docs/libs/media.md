# media

## 模块定位

`media` 是重构后的通用媒体核心，承载视频帧内存、主/子码流缓存、HLS/FLV/MJPEG
输出数据和协议帧订阅。它不启动设备硬件、不解析 HTTP/RTSP/WebRTC 请求，也不依赖
`device`、`hisi_vendor` 或配置系统。

## 核心职责

- 定义 `Codec`、`FrameBuffer`、`FrameSlice`、`EncodedFrame` 等通用媒体类型。
- 通过 `FrameSink::PushFrame()` 接收设备侧输出的编码帧。
- 维护主/子码流的 GOP cache、HLS segment、FLV 起播缓存、MJPEG latest frame 和
  帧订阅 live queue。
- 通过 `KeyFrameRequestCallback` 把协议侧新订阅、恢复等关键帧请求回调给设备层；
  `media` 自身不直接依赖设备模块。
- 对协议模块暴露 `MediaStreams`、`MediaStreamInfo`、`MediaStreamCounters`、
  `FrameSubscription` 相关接口。
- 帧订阅起播 GOP 和 live frame 直接返回带引用计数 payload 的 `EncodedFrame`；
  参数集、codec generation 和 90kHz clock rate 等播放元信息收敛在
  `MediaStreamInfo`。

## 状态与资源模型

`MediaStreams` 拥有码流缓存和订阅队列。输入帧使用 `FrameBuffer` 引用计数保活，
协议订阅和 FLV GOP cache 只增加引用，不复制整帧 payload；HLS segment 是独立
转封装后的 TS buffer。

codec 切换、stream stop 和 timestamp reset 会清理 GOP、HLS、FLV、MJPEG 和
订阅 live queue，后续从新的关键帧重新建立可播放状态。

`MediaFrame` 和 `MediaTrack` 不再作为公共 API 存在。订阅方需要保存帧时只保留
`EncodedFrame` 引用，发送完成后调用对应 unref 接口释放。

`FrameSubscriptionStartData::track_ready` 表示订阅起播数据已具备协议输出条件，
不要用设备运行态替代该判断。

`FramePayload`、NAL 解析结果和时间戳修正器只属于 `media` 内部实现，不进入 public
header；设备侧只需要实现 `FrameSink::PushFrame()`。

## 非目标

- 不直接调用 HiSilicon SDK。
- 不拥有设备启动、停止、抓图、图像参数或区域叠加。
- 不拥有 HTTP/RTSP/WebRTC socket、会话或认证状态。
