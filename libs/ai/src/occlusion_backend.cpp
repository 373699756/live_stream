#include "occlusion_backend.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace live_stream {
namespace ai_internal {
namespace {

#if LIVE_STREAM_HAS_HISI_NNIE
constexpr uint32_t kIveImageAlign = 16;
constexpr uint32_t kGridWidth = 8;
constexpr uint32_t kGridHeight = 8;
constexpr uint32_t kGridSize = kGridWidth * kGridHeight;
constexpr uint32_t kHitThreshold = kGridSize / 2U;
constexpr int32_t kLineMean0 = 80;
constexpr int32_t kLineSigma0 = 0;
constexpr int32_t kLineMean1 = 80;
constexpr int32_t kLineSigma1 = 20;
constexpr HI_U64 kIntegSumMask = 0x0fffffffULL;
constexpr uint32_t kIntegSquareShift = 28;
#endif

}  // namespace

bool OcclusionBackend::Start(const AiModelConfig &config) {
    if (config.input_width == 0 || config.input_height == 0) {
        return false;
    }
#if LIVE_STREAM_HAS_HISI_NNIE
    started_ = false;
    std::memset(&integ_ctrl_, 0, sizeof(integ_ctrl_));
    integ_ctrl_.enOutCtrl = IVE_INTEG_OUT_CTRL_COMBINE;
    started_ = true;
    return true;
#else
    (void)config;
    return false;
#endif
}

void OcclusionBackend::Stop() {
#if LIVE_STREAM_HAS_HISI_NNIE
    FreeWorkspace();
    std::memset(&integ_ctrl_, 0, sizeof(integ_ctrl_));
    sequence_ = 0;
#endif
    started_ = false;
}

AiInferenceResult OcclusionBackend::Run(const hisisdk::YuvFrame &frame,
                                        StreamId stream_id,
                                        const AiModelConfig &config) {
    AiInferenceResult result;
    result.stream_id = stream_id;
    result.pts_us = frame.pts_us;
#if LIVE_STREAM_HAS_HISI_NNIE
    if (config.max_results == 0 || !CanUseFrame(frame) ||
        !EnsureWorkspace(frame.width, frame.height)) {
        return result;
    }
    if (!CopyFrameLuma(frame, &src_image_)) {
        return result;
    }

    std::memset(integ_image_.vir_addr, 0, integ_image_.size);
    IVE_HANDLE handle = 0;
    const HI_S32 ret = HI_MPI_IVE_Integ(
        &handle, &src_image_.image, &integ_image_.image, &integ_ctrl_, HI_TRUE);
    if (ret != HI_SUCCESS || !QueryIveTask(handle)) {
        return result;
    }

    result.success = true;
    result.sequence = ++sequence_;
    const uint32_t hit_size = HitSize();
    if (hit_size > kHitThreshold) {
        AiDetection detection;
        detection.label = "occlusion";
        detection.confidence =
            static_cast<float>(hit_size) / static_cast<float>(kGridSize);
        detection.x = 0.0f;
        detection.y = 0.0f;
        detection.width = 1.0f;
        detection.height = 1.0f;
        if (detection.confidence >= config.confidence_threshold) {
            result.detections.push_back(detection);
        }
    }
#else
    (void)config;
#endif
    return result;
}

#if LIVE_STREAM_HAS_HISI_NNIE
void OcclusionBackend::FreeWorkspace() {
    FreeImage(&src_image_);
    FreeImage(&integ_image_);
}

void OcclusionBackend::FreeImage(OcclusionImage *image) {
    if (image == nullptr) {
        return;
    }
    if (image->phy_addr != 0 && image->vir_addr != nullptr) {
        HI_MPI_SYS_MmzFree(image->phy_addr, image->vir_addr);
    }
    *image = OcclusionImage{};
}

bool OcclusionBackend::EnsureWorkspace(uint32_t width, uint32_t height) {
    if (src_image_.phy_addr != 0 && src_image_.vir_addr != nullptr &&
        src_image_.width == width && src_image_.height == height &&
        integ_image_.phy_addr != 0 && integ_image_.vir_addr != nullptr &&
        integ_image_.width == width && integ_image_.height == height) {
        return true;
    }
    if (width < kGridWidth || height < kGridHeight) {
        return false;
    }
    FreeWorkspace();
    if (!AllocImage(IVE_IMAGE_TYPE_U8C1, static_cast<uint32_t>(sizeof(HI_U8)),
                    "LIVE_AI_OD_SRC", width, height, &src_image_)) {
        FreeWorkspace();
        return false;
    }
    if (!AllocImage(IVE_IMAGE_TYPE_U64C1,
                    static_cast<uint32_t>(sizeof(HI_U64)),
                    "LIVE_AI_OD_INTEG", width, height, &integ_image_)) {
        FreeWorkspace();
        return false;
    }
    return true;
}

bool OcclusionBackend::AllocImage(IVE_IMAGE_TYPE_E image_type,
                                  uint32_t element_size,
                                  const char *mmz_name,
                                  uint32_t width,
                                  uint32_t height,
                                  OcclusionImage *image) {
    if (image == nullptr || element_size == 0 || width == 0 || height == 0) {
        return false;
    }
    const uint32_t stride = AlignUpU32(width, kIveImageAlign);
    const uint64_t image_size =
        static_cast<uint64_t>(stride) * height * element_size;
    if (stride < width || image_size == 0 || image_size > kMaxHiU32) {
        return false;
    }
    HI_U64 phy_addr = 0;
    HI_VOID *vir_addr = nullptr;
    const HI_S32 ret =
        HI_MPI_SYS_MmzAlloc(&phy_addr, &vir_addr, mmz_name, nullptr,
                            static_cast<HI_U32>(image_size));
    if (ret != HI_SUCCESS || phy_addr == 0 || vir_addr == nullptr) {
        return false;
    }
    std::memset(vir_addr, 0, static_cast<size_t>(image_size));
    image->phy_addr = phy_addr;
    image->vir_addr = vir_addr;
    image->size = static_cast<uint32_t>(image_size);
    image->width = width;
    image->height = height;
    image->stride = stride;
    IVE_IMAGE_S &ive_image = image->image;
    std::memset(&ive_image, 0, sizeof(ive_image));
    ive_image.enType = image_type;
    ive_image.u32Width = width;
    ive_image.u32Height = height;
    ive_image.au32Stride[0] = stride;
    ive_image.au64PhyAddr[0] = phy_addr;
    ive_image.au64VirAddr[0] =
        static_cast<HI_U64>(reinterpret_cast<HI_UL>(vir_addr));
    return true;
}

bool OcclusionBackend::CopyFrameLuma(const hisisdk::YuvFrame &frame,
                                     OcclusionImage *image) const {
    if (image == nullptr || image->phy_addr == 0 || image->vir_addr == nullptr ||
        image->width != frame.width || image->height != frame.height ||
        !CanUseFrame(frame)) {
        return false;
    }
    IVE_SRC_DATA_S src{};
    src.u64PhyAddr = frame.mpp_info.phy_addr[0];
    src.u32Width = frame.width;
    src.u32Height = frame.height;
    src.u32Stride = frame.mpp_info.stride[0];
    IVE_DST_DATA_S dst{};
    dst.u64PhyAddr = image->image.au64PhyAddr[0];
    dst.u32Width = image->image.u32Width;
    dst.u32Height = image->image.u32Height;
    dst.u32Stride = image->image.au32Stride[0];
    IVE_DMA_CTRL_S ctrl{};
    ctrl.enMode = IVE_DMA_MODE_DIRECT_COPY;
    IVE_HANDLE handle = 0;
    const HI_S32 ret = HI_MPI_IVE_DMA(&handle, &src, &dst, &ctrl, HI_TRUE);
    if (ret != HI_SUCCESS) {
        return false;
    }
    return QueryIveTask(handle);
}

bool OcclusionBackend::CanUseFrame(const hisisdk::YuvFrame &frame) const {
    const hisisdk::MppYuvFrameInfo &info = frame.mpp_info;
    return started_ && info.valid && frame.width != 0 && frame.height != 0 &&
           info.phy_addr[0] != 0 && info.stride[0] >= frame.width &&
           info.pixel_format ==
               static_cast<int32_t>(PIXEL_FORMAT_YVU_SEMIPLANAR_420) &&
           info.compress_mode == static_cast<int32_t>(COMPRESS_MODE_NONE);
}

uint32_t OcclusionBackend::HitSize() const {
    if (integ_image_.vir_addr == nullptr || integ_image_.width < kGridWidth ||
        integ_image_.height < kGridHeight) {
        return 0;
    }
    const uint32_t block_width = integ_image_.width / kGridWidth;
    const uint32_t block_height = integ_image_.height / kGridHeight;
    if (block_width == 0 || block_height == 0) {
        return 0;
    }

    uint32_t hit_size = 0;
    const HI_U64 *integral =
        static_cast<const HI_U64 *>(integ_image_.vir_addr);
    for (uint32_t grid_y = 0; grid_y < kGridHeight; ++grid_y) {
        for (uint32_t grid_x = 0; grid_x < kGridWidth; ++grid_x) {
            uint32_t mean = 0;
            uint32_t sigma = 0;
            if (ReadBlockStats(integral, block_width, block_height, grid_x,
                               grid_y, &mean, &sigma) &&
                IsOcclusionBlock(mean, sigma)) {
                ++hit_size;
            }
        }
    }
    return hit_size;
}

bool OcclusionBackend::ReadBlockStats(const HI_U64 *integral,
                                      uint32_t block_width,
                                      uint32_t block_height,
                                      uint32_t grid_x,
                                      uint32_t grid_y,
                                      uint32_t *mean,
                                      uint32_t *sigma) const {
    if (integral == nullptr || mean == nullptr || sigma == nullptr ||
        block_width == 0 || block_height == 0) {
        return false;
    }
    const uint32_t left = grid_x * block_width;
    const uint32_t top = grid_y * block_height;
    const uint32_t right = (grid_x + 1U) * block_width - 1U;
    const uint32_t bottom = (grid_y + 1U) * block_height - 1U;
    const uint32_t stride = integ_image_.stride;
    const HI_U64 *top_row =
        top == 0 ? integral : integral + (top - 1U) * stride;
    const HI_U64 *bottom_row = integral + bottom * stride;
    const HI_U64 top_left = top == 0 || left == 0 ? 0 : top_row[left - 1U];
    const HI_U64 top_right = top == 0 ? 0 : top_row[right];
    const HI_U64 bottom_left = left == 0 ? 0 : bottom_row[left - 1U];
    const HI_U64 bottom_right = bottom_row[right];
    const int64_t block_sum =
        static_cast<int64_t>(top_left & kIntegSumMask) +
        static_cast<int64_t>(bottom_right & kIntegSumMask) -
        static_cast<int64_t>(bottom_left & kIntegSumMask) -
        static_cast<int64_t>(top_right & kIntegSumMask);
    const int64_t block_square =
        static_cast<int64_t>(top_left >> kIntegSquareShift) +
        static_cast<int64_t>(bottom_right >> kIntegSquareShift) -
        static_cast<int64_t>(bottom_left >> kIntegSquareShift) -
        static_cast<int64_t>(top_right >> kIntegSquareShift);
    if (block_sum < 0 || block_square < 0) {
        return false;
    }
    const double block_area =
        static_cast<double>(block_width) * static_cast<double>(block_height);
    const double mean_value = static_cast<double>(block_sum) / block_area;
    const double variance =
        static_cast<double>(block_square) / block_area -
        mean_value * mean_value;
    *mean = static_cast<uint32_t>(mean_value);
    *sigma = static_cast<uint32_t>(std::sqrt(std::max(0.0, variance)));
    return true;
}

bool OcclusionBackend::IsOcclusionBlock(uint32_t mean, uint32_t sigma) const {
    const int64_t line_mean_delta = kLineMean1 - kLineMean0;
    if (line_mean_delta == 0) {
        return static_cast<int64_t>(mean) <= kLineMean0;
    }
    const int64_t lhs =
        (static_cast<int64_t>(sigma) - kLineSigma0) * line_mean_delta;
    const int64_t rhs = (static_cast<int64_t>(mean) - kLineMean0) *
                        (kLineSigma1 - kLineSigma0);
    return lhs <= rhs;
}
#endif

}  // namespace ai_internal
}  // namespace live_stream
