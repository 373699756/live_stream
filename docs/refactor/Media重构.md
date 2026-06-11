# libs 合并与媒体/设备命名重构计划

## Summary

目标是把当前 `device_media`、`media_source`、`media_pipeline` 的混杂设计收敛成清晰的
`media` + `device` 两个核心库。

重构重点：

- `media/`：通用媒体帧、码流缓存、协议订阅、HLS/FLV/MJPEG 输出。
- `device/`：设备媒体链路、图像、区域、抓图、硬件配置。
- `platform_hisi/`：HiSilicon SDK 适配，不保存业务状态。
- 删除 `media_pipeline` 混合层，由 `app` 负责组合 `DeviceMedia` 和 `MediaStreams`。
- 命名以业务直观为准，不使用 `Store`、`Runtime`、`Manager`、`Context`、`Topology`。

## Naming Rules

- 不使用 `Dev`，跨模块设备公共类型使用完整 `Device`。
- `device/` 模块内部不重复加 `Device`，例如 `Hardware`、`VideoConfig`、`ImageConfig`、
  `RegionConfig`。
- 媒体公共类型使用 `Media*`，不用 `Live*`。
- 保留 `FrameType`，不改成 `FrameKind`。
- `VideoCodec` 改成 `Codec`，因为当前产品没有音频。
- `VideoBuffer` 改成 `FrameBuffer`。
- `BufferSlice` 改成 `FrameSlice`。
- 对外展示/查询用 `Info`，不用 `Status`。
- 累计计数用 `Counters`，不用 `Stats`。
- 内部锁内变量/状态机才用 `State`，不进公共 API。
- 硬件 VI/VPSS/VENC 通道布局用 `Hardware`，不用 `Topology`。
- 帧订阅统一使用 `FrameSubscription`、`SubscribeFrames(...)`、
  `UnsubscribeFrames(...)`，不再混用 `Attach/Detach`。

## New libs Layout

### `libs/media/`

合并来源：

- `libs/media_source`
- `libs/device_media/include/media`
- `libs/media_pipeline` 中与 GOP、HLS、FLV、MJPEG、帧订阅、媒体信息、计数相关的代码

职责：

- 定义通用媒体类型。
- 接收设备输出的编码帧。
- 维护主/辅码流缓存。
- 提供 HLS、FLV、MJPEG 输出数据。
- 提供 RTSP/WebRTC 使用的帧订阅。
- 提供媒体信息和计数查询。

核心类型：

```cpp
enum class Codec {
    kH264,
    kH265,
    kMjpeg,
    kJpeg,
};

enum class FrameType {
    kIdr,
    kI,
    kP,
    kB,
    kJpeg,
};

struct FrameSlice {
    FrameBuffer *buffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct EncodedFrame {
    StreamId stream_id = StreamId::kMain;
    Codec codec = Codec::kH264;
    FrameType frame_type = FrameType::kP;
    FrameSequence sequence = 0;
    int64_t pts_us = 0;
    int64_t dts_us = 0;
    FrameSlice payload;
};
```

主要公共类型：

- `StreamId`
- `Codec`
- `FrameType`
- `FrameBuffer`
- `FrameSlice`
- `EncodedFrame`
- `KeyFrameRequestType`
- `MediaStreams`
- `MediaStreamInfo`
- `MediaStreamCounters`
- `FrameSubscription`
- `FrameSink`

`FrameSink` 表示接收设备编码帧的一端：

```cpp
class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual bool PushFrame(const EncodedFrame &frame) = 0;
};
```

`MediaStreams` 负责主/辅码流：

```cpp
class MediaStreams : public FrameSink {
public:
    bool PushFrame(const EncodedFrame &frame) override;

    FrameSubscriptionId SubscribeFrames(
        const FrameSubscriptionOptions &options);
    bool UnsubscribeFrames(FrameSubscriptionId id);

    MediaStreamInfo GetStreamInfo(StreamId stream_id) const;
    MediaStreamCounters GetStreamCounters(StreamId stream_id) const;

    bool RequestKeyFrame(StreamId stream_id, KeyFrameRequestType request_type);

    MediaHlsPlaylist GetHlsPlaylist(StreamId stream_id) const;
    MediaSegmentRef GetHlsSegment(StreamId stream_id, uint64_t sequence) const;
    MediaFlvStartData GetFlvStartData(StreamId stream_id) const;
};
```

删除或合并：

- `MediaFrame` 合并进 `EncodedFrame`。
- `MediaTrack` 合并进 `MediaStreamInfo`。
- `MediaSource` 改为 `MediaStreams`。
- `MediaSourceStatus` 改为 `MediaStreamInfo`。
- `MediaSourceStats` 改为 `MediaStreamCounters`。
- `MediaFrameReader` 改为 `FrameSubscription`。
- `AttachFrameReader` 改为 `SubscribeFrames`。
- `DetachFrameReader` 改为 `UnsubscribeFrames`。

`media/` 不允许依赖：

- `device`
- `platform_hisi`
- HTTP/RTSP/WebRTC/ONVIF
- 配置系统
- HiSilicon SDK

### `libs/device/`

合并来源：

- `libs/device_media` 中设备生命周期、配置、图像策略、VENC/VI/VPSS 控制相关代码
- `libs/snapshot`
- `libs/region`
- `libs/media_pipeline` 中设备配置、启动顺序、关键帧请求、设备状态相关代码

职责：

- 启动/停止设备媒体链路。
- 管理 VI/VPSS/VENC 硬件配置。
- 应用主/辅码流编码配置。
- 管理图像参数。
- 管理 OSD/隐私遮挡/区域叠加。
- 抓图。
- 请求关键帧。
- 输出 `EncodedFrame` 到 `FrameSink`。

跨模块公共类型：

- `DeviceMedia`
- `DeviceInfo`
- `DeviceCapabilities`

`device/` 内部主要类型：

- `Hardware`：VI/VPSS/VENC pipe/channel/stream 配置。
- `VideoConfig`：主/辅码流编码配置。
- `ImageConfig`：图像参数配置。
- `ImageInfo`：图像策略当前信息。
- `RegionConfig`：OSD/隐私遮挡/区域叠加配置。
- `SnapshotRequest`
- `SnapshotFrame`

## Boundary Cleanup

- `SnapshotFrame`、`hisisdk::JpegFrame`、`EncodedFrame` 统一使用
  `FrameBuffer + FrameSlice` 表达帧数据。
- `RegionConfig` 和 `hisisdk::RegionConfig` 保持两层：业务层一个，SDK 适配层一个，
  中间只做边界转换。
- `MediaChannels` 并入 `Hardware` 或 `DeviceInfo`，不再漂在媒体层。
- `ImageStrategyStatus` 改为 `ImageInfo`。
- `FrameAttach` 删除。
- 设备到媒体统一用 `FrameSink::PushFrame`。
- 协议消费统一用 `SubscribeFrames/UnsubscribeFrames`。
- `IMediaPipeline` 删除，不再继承多个媒体接口。
- HLS/FLV/MJPEG 的公共数据结构留在 `media/`，只给 HTTP 媒体模块使用，
  不泄漏到 `device/`。

## Data Flow

最终数据流：

1. `app` 创建 `MediaStreams`。
2. `app` 创建 `DeviceMedia`。
3. `app` 调用 `DeviceMedia::SetFrameSink(&media_streams)`。
4. `DeviceMedia::Start()` 启动 VI/VPSS/VENC。
5. VENC 输出 `EncodedFrame`。
6. `DeviceMedia` 调用 `FrameSink::PushFrame(frame)`。
7. `MediaStreams` 更新主/辅码流信息、计数、GOP、HLS、FLV、MJPEG 和帧订阅队列。
8. HTTP/RTSP/WebRTC 从 `MediaStreams` 消费播放数据。
9. API 从 `MediaStreams` 查询 `MediaStreamInfo` / `MediaStreamCounters`。
10. API 从 `DeviceMedia` 查询 `DeviceInfo` / `DeviceCapabilities`。

依赖方向：

- `media` 不依赖业务模块。
- `device -> media`
- `device -> platform_hisi`
- `platform_hisi -> media`
- `http_media/rtsp/webrtc -> media`
- `api -> media + device`
- `app -> all`

## Migration Steps

1. 新建 `libs/media`。
2. 迁移并改名通用媒体类型：
   - `VideoCodec` -> `Codec`
   - `VideoBuffer` -> `FrameBuffer`
   - `BufferSlice` -> `FrameSlice`
   - 保留 `StreamId`、`FrameType`、`EncodedFrame`，新增 `KeyFrameRequestType`
3. 修改 `EncodedFrame`，用 `FrameSlice payload` 替代散落的 `buffer/offset/size`。
4. 将 `media_source` 的 GOP/HLS/FLV/MJPEG/帧队列迁入 `media`。
5. 将 `MediaSource` 改为 `MediaStreams`。
6. 将 `MediaSourceStatus` 改为 `MediaStreamInfo`。
7. 将 `MediaSourceStats` 改为 `MediaStreamCounters`。
8. 将 `MediaFrameReader` 改为 `FrameSubscription`。
9. 将 `AttachFrameReader/DetachFrameReader` 改为 `SubscribeFrames/UnsubscribeFrames`。
10. 删除公共 `MediaFrame` 和 `MediaTrack`，必要字段合并进 `EncodedFrame` 和
    `MediaStreamInfo`。
11. 新建 `libs/device`。
12. 迁移 `DeviceMedia` 生命周期、配置、图像、区域、抓图。
13. 删除 `MediaPipelineConfig`，拆为 `Hardware`、`VideoConfig`、`ImageConfig`。
14. 用 `FrameSink::PushFrame` 替代 `FrameAttach`。
15. 修改 HTTP/RTSP/WebRTC 只依赖 `media`。
16. 修改 API 同时依赖 `media` 和 `device`。
17. 修改 `app/runtime`，显式组合 `DeviceMedia -> MediaStreams`。
18. 删除 `libs/media_pipeline`。
19. 第一阶段稳定后，再评估是否把 `hisi_vendor` 改名为 `platform_hisi`。

## Test Plan

构建验证：

- `make -C libs/media`
- `make -C libs/device`
- `make -j2`
- `make host-test`

板端验证：

- 程序启动，HISI MPP 初始化正常。
- 主码流/辅码流正常出帧。
- WebRTC 预览正常。
- RTSP 播放正常。
- HTTP-FLV 正常。
- HLS playlist 和 segment 正常。
- MJPEG 正常。
- 抓图正常。
- OSD/隐私遮挡/区域叠加正常。
- 请求关键帧正常。
- `/api/media/streams`、`/api/ai/status`、`/api/alarm/status` 不因重构异常。

## Assumptions

- 这是一次结构性重构，允许同步修改内部调用方。
- 不保留旧 wrapper、alias、legacy adapter。
- 不主动整理 `tests/` 目录结构，只修正因生产代码变化导致的编译问题。
- `media_codec` 暂时保持独立，后续单独评估是否改名为 `codec`。
- `platform_hisi` 改名作为第二阶段，避免第一阶段同时改太多构建路径。
