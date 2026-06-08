#ifndef LIVE_STREAM_DEVICE_MEDIA_SRC_STUB_HISI_SDK_H_
#define LIVE_STREAM_DEVICE_MEDIA_SRC_STUB_HISI_SDK_H_

#include "hisisdk/hisi_sdk.h"

namespace live_stream {
namespace hisisdk {

// StubHisiSdk implements IHisiSdk with host-side stubs.
// Used when running WITHOUT LIVE_STREAM_ENABLE_HISI_MPP (no real MPP hardware).
// For MPP builds, MppHisiSdk (in libs/hisi_vendor) is the production SDK.
class StubHisiSdk final : public IHisiSdk {
public:
    StubHisiSdk() = default;

    MediaCapabilities GetCapabilities() override;
    bool InitSystem(const MediaPipelineConfig& config) override;
    bool DeinitSystem() override;
    bool StartVi(const MediaPipelineConfig& config) override;
    void StopVi(const MediaPipelineConfig& config) override;
    bool StartVpss(const MediaPipelineConfig& config) override;
    void StopVpss(const MediaPipelineConfig& config) override;
    bool BindViVpss(const MediaPipelineConfig& config) override;
    void UnbindViVpss(const MediaPipelineConfig& config) override;
    bool StartVenc(const MediaPipelineConfig& config) override;
    void StopVenc(const MediaPipelineConfig& config) override;
    bool BindVpssVenc(const MediaPipelineConfig& config) override;
    void UnbindVpssVenc(const MediaPipelineConfig& config) override;
    bool StartVencStream(const MediaPipelineConfig& config,
                         EncodedFrameCallback callback,
                         void* user) override;
    void StopVencStream(const MediaPipelineConfig& config) override;
    bool RequestIdr(int32_t venc_channel) override;
    bool ApplyImageConfig(const MediaPipelineConfig& config,
                          const ConfigJson& image_config) override;
    ExposureInfo QueryExposureInfo(
        const MediaPipelineConfig& config) override;
    bool CreateRegion(int32_t handle,
                      const RegionConfig& config) override;
    bool AttachRegion(int32_t handle,
                      const RegionConfig& config) override;
    bool DetachRegion(int32_t handle,
                      const RegionConfig& config) override;
    bool SetRegionDisplay(int32_t handle,
                          const RegionConfig& config) override;
    bool SetRegionBitmap(int32_t handle,
                         const Bitmap& bitmap) override;
    void DestroyRegion(int32_t handle) override;
    JpegFrame CaptureJpeg(const SnapshotConfig& config) override;
    YuvFrame CaptureYuvFrame(const MppChannel& vpss_channel,
                             Size size,
                             uint32_t timeout_ms) override;
};

}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_MEDIA_SRC_STUB_HISI_SDK_H_
