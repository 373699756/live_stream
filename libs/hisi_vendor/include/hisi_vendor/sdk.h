#ifndef LIVE_STREAM_HISI_VENDOR_SDK_H_
#define LIVE_STREAM_HISI_VENDOR_SDK_H_

#include "media/frame_sink.h"
#include "media/media_buffer.h"
#include "hisi_vendor/media_capabilities.h"
#include "hisi_vendor/mpp_types.h"
#include "hisi_vendor/media_pipeline.h"
#include "json.h"

#include <cstdint>

namespace live_stream {
namespace hisisdk {

enum class RegionType {
    kOverlay = 0,
    kOverlayEx,
    kCover,
    kCoverEx,
    kMosaic,
};

enum class PixelFormat {
    kArgb1555 = 0,
    kArgb4444,
    kArgb8888,
    kArgb2Bpp,
};

struct Point {
    int32_t x = 0;
    int32_t y = 0;
};

struct Size {
    uint32_t width = 0;
    uint32_t height = 0;
};

struct Bitmap {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t stride = 0;
    Size dimensions;
    PixelFormat pixel_format = PixelFormat::kArgb1555;
};

struct RegionConfig {
    RegionType type = RegionType::kOverlay;
    PixelFormat pixel_format = PixelFormat::kArgb1555;
    Size size;
    Point position;
    uint32_t background_color = 0x00ff00ff;
    uint32_t foreground_alpha = 128;
    uint32_t background_alpha = 128;
    bool visible = true;
    MppChannel target;
};

struct SnapshotConfig {
    int32_t snap_pipe = 2;
    int32_t snap_vpss_group = 2;
    int32_t snap_vpss_channel = 0;
    int32_t jpeg_venc_channel = 3;
    Size size;
    uint32_t capture_frames = 1;
    uint32_t repeat_send_times = 1;
    uint32_t timeout_ms = 3000;
    uint32_t jpeg_quality = 90;
    bool load_ccm = true;
    bool zero_shutter_lag = false;
};

struct JpegFrame {
    MediaBufferRef buffer;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts_us = 0;

    bool Valid() const { return buffer.Valid() && buffer.Size() != 0; }
    const uint8_t* Data() const { return buffer.Data(); }
    uint32_t Size() const { return buffer.Size(); }
};

struct MppYuvFrameInfo {
    bool valid = false;
    uint64_t phy_addr[3] = {};
    uint64_t vir_addr[3] = {};
    uint64_t header_phy_addr[3] = {};
    uint64_t header_vir_addr[3] = {};
    uint64_t ext_phy_addr[3] = {};
    uint64_t ext_vir_addr[3] = {};
    uint32_t stride[3] = {};
    uint32_t header_stride[3] = {};
    uint32_t ext_stride[3] = {};
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t pool_id = 0;
    uint32_t max_luminance = 0;
    uint32_t min_luminance = 0;
    uint32_t time_ref = 0;
    uint32_t frame_flag = 0;
    int32_t module_id = 0;
    int32_t field = 0;
    int32_t pixel_format = 0;
    int32_t video_format = 0;
    int32_t compress_mode = 0;
    int32_t dynamic_range = 0;
    int32_t color_gamut = 0;
    int16_t offset_top = 0;
    int16_t offset_bottom = 0;
    int16_t offset_left = 0;
    int16_t offset_right = 0;
};

struct YuvFrame {
    MediaBufferRef buffer;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride_y = 0;
    uint32_t stride_uv = 0;
    int64_t pts_us = 0;
    MppYuvFrameInfo mpp_info;

    bool Valid() const { return buffer.Valid() && buffer.Size() != 0; }
    const uint8_t* Data() const { return buffer.Data(); }
    uint32_t Size() const { return buffer.Size(); }
};

struct ExposureInfo {
    bool valid = false;
    uint32_t exposure_time_us = 0;
    uint32_t analog_gain = 0;
    uint32_t digital_gain = 0;
    uint32_t isp_digital_gain = 0;
    uint32_t iso = 0;
};

class IHisiSystem {
public:
    virtual ~IHisiSystem() = default;

    virtual MediaCapabilities GetCapabilities() = 0;

    virtual bool InitSystem(const MediaPipelineConfig& config) = 0;
    virtual bool DeinitSystem() = 0;
};

class IHisiMediaPipeline {
public:
    virtual ~IHisiMediaPipeline() = default;

    virtual bool StartVi(const MediaPipelineConfig& config) = 0;
    virtual void StopVi(const MediaPipelineConfig& config) = 0;
    virtual bool StartVpss(const MediaPipelineConfig& config) = 0;
    virtual void StopVpss(const MediaPipelineConfig& config) = 0;
    virtual bool BindViVpss(const MediaPipelineConfig& config) = 0;
    virtual void UnbindViVpss(const MediaPipelineConfig& config) = 0;
    virtual bool StartVenc(const MediaPipelineConfig& config) = 0;
    virtual void StopVenc(const MediaPipelineConfig& config) = 0;
    virtual bool BindVpssVenc(const MediaPipelineConfig& config) = 0;
    virtual void UnbindVpssVenc(const MediaPipelineConfig& config) = 0;
};

class IHisiVencStream {
public:
    virtual ~IHisiVencStream() = default;

    virtual bool StartVencStream(
        const MediaPipelineConfig& config,
        MediaFrameCallback callback,
        void* user) = 0;
    virtual void StopVencStream(const MediaPipelineConfig& config) = 0;
    virtual bool RequestIdr(int32_t venc_channel) = 0;
    virtual bool ApplyVencRoi(int32_t venc_channel,
                              const VideoStreamConfig& stream_config) = 0;
};

class IHisiImage {
public:
    virtual ~IHisiImage() = default;

    virtual bool ApplyImageConfig(const MediaPipelineConfig& config,
                                  const Json& image_config) = 0;
    virtual ExposureInfo QueryExposureInfo(
        const MediaPipelineConfig& config) = 0;
};

class IHisiRegion {
public:
    virtual ~IHisiRegion() = default;

    virtual bool CreateRegion(int32_t handle,
                              const RegionConfig& config) = 0;
    virtual bool AttachRegion(int32_t handle,
                              const RegionConfig& config) = 0;
    virtual bool DetachRegion(int32_t handle,
                              const RegionConfig& config) = 0;
    virtual bool SetRegionDisplay(int32_t handle,
                                  const RegionConfig& config) = 0;
    virtual bool SetRegionBitmap(int32_t handle,
                                 const Bitmap& bitmap) = 0;
    virtual void DestroyRegion(int32_t handle) = 0;
};

class IHisiSnapshot {
public:
    virtual ~IHisiSnapshot() = default;

    virtual JpegFrame CaptureJpeg(const SnapshotConfig& config) = 0;
    virtual YuvFrame CaptureYuvFrame(const MppChannel& vpss_channel,
                                     Size size,
                                     uint32_t timeout_ms) = 0;
};

struct HisiSdk {
    IHisiSystem *system = nullptr;
    IHisiMediaPipeline *media_pipeline = nullptr;
    IHisiVencStream *venc_stream = nullptr;
    IHisiRegion *region = nullptr;
    IHisiSnapshot *snapshot = nullptr;
    IHisiImage *image = nullptr;
};

}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SDK_H_
