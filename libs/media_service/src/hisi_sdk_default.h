#ifndef LIVE_STREAM_MEDIA_SERVICE_SRC_HISI_SDK_DEFAULT_H_
#define LIVE_STREAM_MEDIA_SERVICE_SRC_HISI_SDK_DEFAULT_H_

#include "hisisdk/hisi_sdk.h"

namespace live_stream {
namespace hisisdk {

class DefaultHisiSdk final : public IHisiSdk {
 public:
    infra::Result<MediaCapabilities> GetCapabilities() override;

    infra::Status InitSystem(const MediaPipelineConfig& config) override;
    void DeinitSystem() override;

    infra::Status StartVi(const MediaPipelineConfig& config) override;
    void StopVi(const MediaPipelineConfig& config) override;
    infra::Status StartVpss(const MediaPipelineConfig& config) override;
    void StopVpss(const MediaPipelineConfig& config) override;
    infra::Status BindViVpss(const MediaPipelineConfig& config) override;
    void UnbindViVpss(const MediaPipelineConfig& config) override;
    infra::Status StartVenc(const MediaPipelineConfig& config) override;
    void StopVenc(const MediaPipelineConfig& config) override;
    infra::Status BindVpssVenc(const MediaPipelineConfig& config) override;
    void UnbindVpssVenc(const MediaPipelineConfig& config) override;
    infra::Status StartVencStream(const MediaPipelineConfig& config) override;
    void StopVencStream(const MediaPipelineConfig& config) override;
    infra::Status RequestIdr(int32_t venc_channel) override;

    infra::Status CreateRegion(int32_t handle,
                               const RegionConfig& config) override;
    infra::Status AttachRegion(int32_t handle,
                               const RegionConfig& config) override;
    infra::Status DetachRegion(int32_t handle,
                               const RegionConfig& config) override;
    infra::Status SetRegionDisplay(int32_t handle,
                                   const RegionConfig& config) override;
    infra::Status SetRegionBitmap(int32_t handle,
                                  const Bitmap& bitmap) override;
    void DestroyRegion(int32_t handle) override;

    infra::Result<JpegFrame> CaptureJpeg(
        const SnapshotConfig& config) override;
};

}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SERVICE_SRC_HISI_SDK_DEFAULT_H_
