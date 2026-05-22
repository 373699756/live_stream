#ifndef LIVE_STREAM_HISISDK_HISI_SDK_H_
#define LIVE_STREAM_HISISDK_HISI_SDK_H_

#include "media/frame_attach.h"
#include "media/media_buffer.h"
#include "media/media_capabilities.h"
#include "media/mpp_types.h"
#include "media/pipeline_config.h"
#include "config_json.h"

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
    uint32_t frame_count = 1;
    uint32_t repeat_send_times = 1;
    uint32_t timeout_ms = 3000;
    uint32_t jpeg_quality = 90;
    bool load_ccm = true;
    bool zero_shutter_lag = false;
};

struct JpegFrame {
    VideoBuffer* buffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts_us = 0;

    JpegFrame() = default;
    JpegFrame(const JpegFrame& other)
        : buffer(VideoBufferRetain(other.buffer)),
          offset(other.offset),
          size(other.size),
          width(other.width),
          height(other.height),
          pts_us(other.pts_us) {}
    JpegFrame& operator=(const JpegFrame& other) {
        if (this == &other) {
            return *this;
        }
        VideoBuffer* retained = VideoBufferRetain(other.buffer);
        VideoBufferRelease(buffer);
        buffer = retained;
        offset = other.offset;
        size = other.size;
        width = other.width;
        height = other.height;
        pts_us = other.pts_us;
        return *this;
    }
    JpegFrame(JpegFrame&& other) noexcept
        : buffer(other.buffer),
          offset(other.offset),
          size(other.size),
          width(other.width),
          height(other.height),
          pts_us(other.pts_us) {
        other.buffer = nullptr;
        other.offset = 0;
        other.size = 0;
    }
    JpegFrame& operator=(JpegFrame&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        VideoBufferRelease(buffer);
        buffer = other.buffer;
        offset = other.offset;
        size = other.size;
        width = other.width;
        height = other.height;
        pts_us = other.pts_us;
        other.buffer = nullptr;
        other.offset = 0;
        other.size = 0;
        return *this;
    }
    ~JpegFrame() { VideoBufferRelease(buffer); }
};

struct YuvFrame {
    VideoBuffer* buffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride_y = 0;
    uint32_t stride_uv = 0;
    int64_t pts_us = 0;

    YuvFrame() = default;
    YuvFrame(const YuvFrame& other)
        : buffer(VideoBufferRetain(other.buffer)),
          offset(other.offset),
          size(other.size),
          width(other.width),
          height(other.height),
          stride_y(other.stride_y),
          stride_uv(other.stride_uv),
          pts_us(other.pts_us) {}
    YuvFrame& operator=(const YuvFrame& other) {
        if (this == &other) {
            return *this;
        }
        VideoBuffer* retained = VideoBufferRetain(other.buffer);
        VideoBufferRelease(buffer);
        buffer = retained;
        offset = other.offset;
        size = other.size;
        width = other.width;
        height = other.height;
        stride_y = other.stride_y;
        stride_uv = other.stride_uv;
        pts_us = other.pts_us;
        return *this;
    }
    YuvFrame(YuvFrame&& other) noexcept
        : buffer(other.buffer),
          offset(other.offset),
          size(other.size),
          width(other.width),
          height(other.height),
          stride_y(other.stride_y),
          stride_uv(other.stride_uv),
          pts_us(other.pts_us) {
        other.buffer = nullptr;
        other.offset = 0;
        other.size = 0;
    }
    YuvFrame& operator=(YuvFrame&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        VideoBufferRelease(buffer);
        buffer = other.buffer;
        offset = other.offset;
        size = other.size;
        width = other.width;
        height = other.height;
        stride_y = other.stride_y;
        stride_uv = other.stride_uv;
        pts_us = other.pts_us;
        other.buffer = nullptr;
        other.offset = 0;
        other.size = 0;
        return *this;
    }
    ~YuvFrame() { VideoBufferRelease(buffer); }
};

struct ExposureInfo {
    bool valid = false;
    uint32_t exposure_time_us = 0;
    uint32_t analog_gain = 0;
    uint32_t digital_gain = 0;
    uint32_t isp_digital_gain = 0;
    uint32_t iso = 0;
};

class IHisiSdk {
public:
    virtual ~IHisiSdk() = default;

    virtual MediaCapabilities GetCapabilities() = 0;

    virtual bool InitSystem(const MediaPipelineConfig& config) = 0;
    virtual void DeinitSystem() = 0;

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
    virtual bool StartVencStream(
        const MediaPipelineConfig& config,
        EncodedFrameCallback callback,
        void* user) = 0;
    virtual void StopVencStream(const MediaPipelineConfig& config) = 0;
    virtual bool RequestIdr(int32_t venc_channel) = 0;
    virtual bool ApplyImageConfig(const MediaPipelineConfig& config,
                                  const ConfigJson& image_config) = 0;
    virtual ExposureInfo QueryExposureInfo(
        const MediaPipelineConfig& config) = 0;

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

    virtual JpegFrame CaptureJpeg(const SnapshotConfig& config) = 0;
    virtual YuvFrame CaptureYuvFrame(const MppChannel& vpss_channel,
                                     Size size,
                                     uint32_t timeout_ms) = 0;
};

IHisiSdk& DefaultSdk();

}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISISDK_HISI_SDK_H_
