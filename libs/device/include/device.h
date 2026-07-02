#ifndef LIVE_STREAM_DEVICE_DEVICE_H_
#define LIVE_STREAM_DEVICE_DEVICE_H_

#include "hisi_vendor/sdk.h"
#include "media/frame_sink.h"
#include "media/media_buffer.h"
#include "hisi_vendor/media_capabilities.h"
#include "hisi_vendor/mpp_types.h"
#include "hisi_vendor/media_pipeline.h"

#include <cstdint>
#include <memory>
#include <string>

namespace live_stream {

class IConfig;

struct DeviceMediaOptions {
    MediaPipelineConfig default_config;
    int32_t snapshot_venc_channel = 3;
    IConfig *config = nullptr;
    hisisdk::HisiSdk sdk;
};

struct SnapshotConfig {
    int32_t snap_pipe = 2;
    int32_t snap_vpss_group = 2;
    int32_t snap_vpss_channel = 0;
    int32_t jpeg_venc_channel = 3;
    VideoSize size;
    uint32_t capture_frames = 1;
    uint32_t repeat_send_times = 1;
    bool load_ccm = true;
    bool zero_shutter_lag = false;
};

struct SnapshotRequest {
    StreamId stream_id = StreamId::kMain;
    uint32_t timeout_ms = 3000;
    uint32_t jpeg_quality = 90;
    bool include_thumbnail = true;
};

struct SnapshotFrame {
    MediaBufferRef buffer;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts_us = 0;

    MediaBufferRef Payload() const { return buffer; }
    uint32_t Size() const { return buffer.Size(); }

    bool IsPayloadValid() const {
        return buffer.Valid() && buffer.Size() != 0;
    }

    const uint8_t *PayloadData() const {
        return IsPayloadValid() ? buffer.Data() : nullptr;
    }
};

struct SnapshotInfo {
    uint64_t config_applies = 0;
    uint64_t config_apply_failures = 0;
    uint64_t captures = 0;
    uint64_t capture_failures = 0;
    uint32_t jpeg_quality = 90;
    uint32_t timeout_ms = 3000;
    bool enabled = true;
    bool capturing = false;
};

enum class RegionType {
    kOverlay = 0,
    kOverlayEx,
    kCover,
    kCoverEx,
    kMosaic,
};

enum class RegionPixelFormat {
    kArgb1555 = 0,
    kArgb4444,
    kArgb8888,
    kArgb2Bpp,
};

struct RegionPoint {
    int32_t x = 0;
    int32_t y = 0;
};

struct RegionSize {
    uint32_t width = 200;
    uint32_t height = 200;
};

struct RegionBitmap {
    const uint8_t *data = nullptr;
    uint32_t size = 0;
    uint32_t stride = 0;
    RegionSize dimensions;
    RegionPixelFormat pixel_format = RegionPixelFormat::kArgb1555;
};

struct RegionConfig {
    RegionType type = RegionType::kOverlay;
    RegionPixelFormat pixel_format = RegionPixelFormat::kArgb1555;
    RegionSize size;
    RegionPoint position;
    uint32_t background_color = 0x00ff00ff;
    uint32_t foreground_alpha = 128;
    uint32_t background_alpha = 128;
    bool visible = true;
    MppChannel target;
};

struct RegionId {
    uint32_t value = 0;
};

struct OverlayInfo {
    uint64_t config_applies = 0;
    uint64_t config_apply_failures = 0;
    uint64_t bitmap_updates = 0;
    uint32_t region_size = 0;
};

struct ImageInfo {
    bool enabled = false;
    bool active = false;
    bool exposure_valid = false;
    uint32_t iso = 0;
    uint32_t exposure_time_us = 0;
    uint32_t analog_gain = 0;
    uint32_t digital_gain = 0;
    uint32_t isp_digital_gain = 0;
    std::string mode;
    std::string tier;
    std::string requested_tier;
    std::string pending_tier;
    int32_t pending_tier_hits = 0;
    int32_t tier_stability_samples = 0;
    int32_t saturation = 0;
    int32_t sharpness = 0;
    int32_t denoise_2d = 0;
    int32_t denoise_3d = 0;
    int32_t gamma = 0;
};

class DeviceMedia {
public:
    virtual ~DeviceMedia() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsStarted() const = 0;
    virtual bool IsRestarting() const = 0;
    virtual bool IsStreamStarted(StreamId stream_id) const = 0;
    virtual Codec GetStreamCodec(StreamId stream_id) const = 0;
    virtual bool SetFrameSink(FrameSink *sink) = 0;
    virtual bool RequestKeyframe(StreamId stream_id,
                                 KeyframeRequestSource source) = 0;
    virtual MediaCapabilities GetCapabilities() const = 0;
    virtual MediaChannels GetChannels() const = 0;
    virtual ImageInfo GetImageInfo() const = 0;
    virtual SnapshotFrame CaptureSnapshot(
        const SnapshotRequest &request) = 0;
    virtual SnapshotInfo GetSnapshotInfo() const = 0;
    virtual OverlayInfo GetOverlayInfo() const = 0;
};

std::unique_ptr<DeviceMedia> CreateDeviceMedia();
std::unique_ptr<DeviceMedia> CreateDeviceMedia(
    const MediaPipelineConfig &config);
std::unique_ptr<DeviceMedia> CreateDeviceMedia(
    const DeviceMediaOptions &options);

}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_DEVICE_H_
