#ifndef LIVE_STREAM_HISI_VENDOR_MPP_SDK_H_
#define LIVE_STREAM_HISI_VENDOR_MPP_SDK_H_

#include "hisi_vendor/sdk.h"

namespace live_stream {
namespace hisisdk {

struct MppHisiSdkImpl;

// MppHisiSdk implements the narrow HiSilicon SDK interfaces through MPP APIs.
// This is the production implementation for Hi3516CV500 / Hi3516DV300 platforms.
// Use it in HISI MPP builds. Host builds inject device's internal host SDK.
class MppHisiSdk final : public IHisiSystem,
                         public IHisiMediaPipeline,
                         public IHisiVencStream,
                         public IHisiRegion,
                         public IHisiSnapshot,
                         public IHisiImage {
public:
    MppHisiSdk();
    ~MppHisiSdk() override;

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
                         MediaFrameCallback callback,
                         void* user) override;
    void StopVencStream(const MediaPipelineConfig& config) override;
    bool RequestIdr(int32_t venc_channel) override;
    bool ApplyVencRoi(int32_t venc_channel,
                      const VideoStreamConfig& stream_config) override;
    bool ApplyImageConfig(const MediaPipelineConfig& config,
                          const Json& image_config) override;
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

private:
    MppHisiSdkImpl* impl_;
};

// Factory function – returns narrow pointers to a static MppHisiSdk instance.
HisiSdk MppSdk();

}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_MPP_SDK_H_
