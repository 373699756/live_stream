#ifndef LIVE_STREAM_AI_SRC_OCCLUSION_BACKEND_H_
#define LIVE_STREAM_AI_SRC_OCCLUSION_BACKEND_H_

#include "ai.h"
#include "hisi_ai_platform.h"
#include "hisi_vendor/sdk.h"

namespace live_stream {
namespace ai_internal {

class OcclusionBackend {
public:
    bool Start(const AiModelConfig &config);
    void Stop();
    bool Started() const { return started_; }
    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config);

private:
#if LIVE_STREAM_HAS_HISI_NNIE
    struct OcclusionImage {
        IVE_IMAGE_S image{};
        HI_U64 phy_addr = 0;
        HI_VOID *vir_addr = nullptr;
        uint32_t size = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t stride = 0;
    };

    void FreeWorkspace();
    void FreeImage(OcclusionImage *image);
    bool EnsureWorkspace(uint32_t width, uint32_t height);
    bool AllocImage(IVE_IMAGE_TYPE_E image_type,
                    uint32_t element_size,
                    const char *mmz_name,
                    uint32_t width,
                    uint32_t height,
                    OcclusionImage *image);
    bool CopyFrameLuma(const hisisdk::YuvFrame &frame,
                       OcclusionImage *image) const;
    bool CanUseFrame(const hisisdk::YuvFrame &frame) const;
    uint32_t OccludedBlockTotal() const;
    bool ReadBlockStats(const HI_U64 *integral,
                        uint32_t block_width,
                        uint32_t block_height,
                        uint32_t grid_x,
                        uint32_t grid_y,
                        uint32_t *mean,
                        uint32_t *sigma) const;
    bool IsOcclusionBlock(uint32_t mean, uint32_t sigma) const;

    OcclusionImage src_image_;
    OcclusionImage integ_image_;
    IVE_INTEG_CTRL_S integ_ctrl_{};
    FrameSequence sequence_ = 0;
#endif
    bool started_ = false;
};

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_OCCLUSION_BACKEND_H_
