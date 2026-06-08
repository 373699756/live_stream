#ifndef LIVE_STREAM_AI_SRC_MOTION_BACKEND_H_
#define LIVE_STREAM_AI_SRC_MOTION_BACKEND_H_

#include "ai.h"
#include "hisi_ai_platform.h"
#include "hisisdk/hisi_sdk.h"

#include <array>
#include <vector>

namespace live_stream {
namespace ai_internal {

class MotionBackend {
public:
    bool Start(const AiModelConfig &config);
    void Stop();
    bool Started() const { return started_; }
    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config);

private:
#if LIVE_STREAM_HAS_HISI_NNIE
    struct MotionImage {
        IVE_IMAGE_S image{};
        HI_U64 phy_addr = 0;
        HI_VOID *vir_addr = nullptr;
        uint32_t size = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t stride = 0;
    };

    struct MotionBlob {
        IVE_DST_MEM_INFO_S mem{};
        HI_VOID *vir_addr = nullptr;
    };

    void FreeWorkspace();
    bool EnsureWorkspace(uint32_t width, uint32_t height);
    bool AllocImage(uint32_t width, uint32_t height, MotionImage *image);
    bool AllocBlob();
    void InitAttr(uint32_t width, uint32_t height);
    bool CopyFrameLuma(const hisisdk::YuvFrame &frame, MotionImage *image) const;
    bool CanUseFrame(const hisisdk::YuvFrame &frame) const;
    std::vector<AiDetection> DecodeBlob(const AiModelConfig &config);
    uint32_t CountRegions(const IVE_CCBLOB_S &blob,
                          HI_U16 area_threshold) const;
    bool IsValidRegion(const IVE_REGION_S &region) const;

    std::array<MotionImage, 2> images_;
    MotionBlob blob_;
    MD_ATTR_S attr_{};
    FrameSequence sequence_ = 0;
    uint32_t current_index_ = 0;
    bool initialized_ = false;
    bool channel_created_ = false;
    bool has_reference_ = false;
#endif
    bool started_ = false;
};

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_MOTION_BACKEND_H_
