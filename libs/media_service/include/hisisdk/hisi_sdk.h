#ifndef LIVE_STREAM_HISISDK_HISI_SDK_H_
#define LIVE_STREAM_HISISDK_HISI_SDK_H_

#include "infra/media_buffer.h"
#include "infra/status.h"
#include "media/frame_source.h"
#include "media/media_capabilities.h"
#include "media/mpp_types.h"
#include "media/pipeline_config.h"

#include <cstdint>
#include <memory>

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
    int32_t jpeg_venc_channel = 1;
    Size size;
    uint32_t frame_count = 1;
    uint32_t repeat_send_times = 1;
    uint32_t timeout_ms = 3000;
    uint32_t jpeg_quality = 90;
    bool load_ccm = true;
    bool zero_shutter_lag = false;
};

struct JpegFrame {
    std::shared_ptr<infra::IMediaBuffer> buffer;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts_us = 0;
};

class IHisiSdk {
 public:
    virtual ~IHisiSdk() = default;

    virtual infra::Result<MediaCapabilities> GetCapabilities() = 0;

    virtual infra::Status InitSystem(const MediaPipelineConfig& config) = 0;
    virtual void DeinitSystem() = 0;

    virtual infra::Status StartVi(const MediaPipelineConfig& config) = 0;
    virtual void StopVi(const MediaPipelineConfig& config) = 0;
    virtual infra::Status StartVpss(const MediaPipelineConfig& config) = 0;
    virtual void StopVpss(const MediaPipelineConfig& config) = 0;
    virtual infra::Status BindViVpss(const MediaPipelineConfig& config) = 0;
    virtual void UnbindViVpss(const MediaPipelineConfig& config) = 0;
    virtual infra::Status StartVenc(const MediaPipelineConfig& config) = 0;
    virtual void StopVenc(const MediaPipelineConfig& config) = 0;
    virtual infra::Status BindVpssVenc(const MediaPipelineConfig& config) = 0;
    virtual void UnbindVpssVenc(const MediaPipelineConfig& config) = 0;
    virtual infra::Status StartVencStream(
        const MediaPipelineConfig& config,
        EncodedFrameCallback callback,
        void* user) = 0;
    virtual void StopVencStream(const MediaPipelineConfig& config) = 0;
    virtual infra::Status RequestIdr(int32_t venc_channel) = 0;

    virtual infra::Status CreateRegion(int32_t handle,
                                       const RegionConfig& config) = 0;
    virtual infra::Status AttachRegion(int32_t handle,
                                       const RegionConfig& config) = 0;
    virtual infra::Status DetachRegion(int32_t handle,
                                       const RegionConfig& config) = 0;
    virtual infra::Status SetRegionDisplay(int32_t handle,
                                           const RegionConfig& config) = 0;
    virtual infra::Status SetRegionBitmap(int32_t handle,
                                          const Bitmap& bitmap) = 0;
    virtual void DestroyRegion(int32_t handle) = 0;

    virtual infra::Result<JpegFrame> CaptureJpeg(
        const SnapshotConfig& config) = 0;
};

IHisiSdk& DefaultSdk();

}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISISDK_HISI_SDK_H_
