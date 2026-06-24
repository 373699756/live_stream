#include "motion_backend.h"

#include "infra/log.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace live_stream {
namespace ai_internal {
namespace {

#if LIVE_STREAM_HAS_HISI_NNIE
constexpr uint32_t kIveImageAlign = 16;
constexpr uint32_t kMotionAreaThresholdStep = 8;
constexpr HI_U16 kMotionSadThreshold = 200;
constexpr HI_U0Q16 kMotionBackgroundBlend = 32768;
constexpr MD_CHN kMotionChannel = 0;
#endif

}  // namespace

bool MotionBackend::Start(const AiModelConfig &config) {
    if (config.input_width == 0 || config.input_height == 0) {
        return false;
    }
#if LIVE_STREAM_HAS_HISI_NNIE
    started_ = false;
    if (HI_IVS_MD_Init() != HI_SUCCESS) {
        Error("ai", "Init IVS motion detection failed");
        return false;
    }
    initialized_ = true;
    started_ = true;
    return true;
#else
    (void)config;
    return false;
#endif
}

void MotionBackend::Stop() {
#if LIVE_STREAM_HAS_HISI_NNIE
    if (channel_created_) {
        const HI_S32 ret = HI_IVS_MD_DestroyChn(kMotionChannel);
        if (ret != HI_SUCCESS) {
            Error("ai", "Destroy IVS motion channel failed: ret=%#x",
                  static_cast<unsigned int>(ret));
        }
        channel_created_ = false;
    }
    if (initialized_) {
        const HI_S32 ret = HI_IVS_MD_Exit();
        if (ret != HI_SUCCESS) {
            Error("ai", "Exit IVS motion detection failed: ret=%#x",
                  static_cast<unsigned int>(ret));
        }
        initialized_ = false;
    }
    FreeWorkspace();
    current_index_ = 0;
    has_reference_ = false;
#endif
    started_ = false;
}

AiInferenceResult MotionBackend::Run(const hisisdk::YuvFrame &frame,
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

    MotionImage &current = images_[current_index_];
    if (!CopyFrameLuma(frame, &current)) {
        return result;
    }
    if (!has_reference_) {
        has_reference_ = true;
        current_index_ = 1U - current_index_;
        result.success = true;
        result.sequence = ++sequence_;
        return result;
    }

    MotionImage &reference = images_[1U - current_index_];
    std::memset(blob_.vir_addr, 0, blob_.mem.u32Size);
    const HI_S32 ret = HI_IVS_MD_Process(
        kMotionChannel, &current.image, &reference.image, nullptr, &blob_.mem);
    if (ret != HI_SUCCESS) {
        return result;
    }
    result.success = true;
    result.sequence = ++sequence_;
    result.detections = DecodeBlob(config);
    current_index_ = 1U - current_index_;
#else
    (void)config;
#endif
    return result;
}

#if LIVE_STREAM_HAS_HISI_NNIE
void MotionBackend::FreeWorkspace() {
    for (MotionImage &image : images_) {
        if (image.phy_addr != 0 && image.vir_addr != nullptr) {
            HI_MPI_SYS_MmzFree(image.phy_addr, image.vir_addr);
        }
        image = MotionImage{};
    }
    if (blob_.mem.u64PhyAddr != 0 && blob_.vir_addr) {
        HI_MPI_SYS_MmzFree(blob_.mem.u64PhyAddr, blob_.vir_addr);
    }
    blob_ = MotionBlob{};
    std::memset(&attr_, 0, sizeof(attr_));
}

bool MotionBackend::EnsureWorkspace(uint32_t width, uint32_t height) {
    if (images_[0].phy_addr != 0 && images_[0].vir_addr != nullptr &&
        images_[0].width == width && images_[0].height == height &&
        blob_.mem.u64PhyAddr != 0) {
        return true;
    }
    if (channel_created_) {
        const HI_S32 ret = HI_IVS_MD_DestroyChn(kMotionChannel);
        if (ret != HI_SUCCESS) {
            Error("ai", "Destroy IVS motion channel failed: ret=%#x",
                  static_cast<unsigned int>(ret));
            return false;
        }
        channel_created_ = false;
    }
    FreeWorkspace();

    for (MotionImage &image : images_) {
        if (!AllocImage(width, height, &image)) {
            FreeWorkspace();
            return false;
        }
    }
    if (!AllocBlob()) {
        FreeWorkspace();
        return false;
    }
    InitAttr(width, height);
    if (HI_IVS_MD_CreateChn(kMotionChannel, &attr_) != HI_SUCCESS) {
        Error("ai", "Create IVS motion channel failed");
        FreeWorkspace();
        return false;
    }
    channel_created_ = true;
    has_reference_ = false;
    current_index_ = 0;
    return true;
}

bool MotionBackend::AllocImage(uint32_t width,
                               uint32_t height,
                               MotionImage *image) {
    if (image == nullptr || width == 0 || height == 0) {
        return false;
    }
    const uint32_t stride = AlignUpU32(width, kIveImageAlign);
    const uint64_t image_size = static_cast<uint64_t>(stride) * height;
    if (stride < width || image_size == 0 || image_size > kMaxHiU32) {
        return false;
    }
    HI_U64 phy_addr = 0;
    HI_VOID *vir_addr = nullptr;
    const HI_S32 ret = HI_MPI_SYS_MmzAlloc(
        &phy_addr, &vir_addr, "LIVE_AI_MD_IMAGE", nullptr,
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
    ive_image.enType = IVE_IMAGE_TYPE_U8C1;
    ive_image.u32Width = width;
    ive_image.u32Height = height;
    ive_image.au32Stride[0] = stride;
    ive_image.au64PhyAddr[0] = phy_addr;
    ive_image.au64VirAddr[0] =
        static_cast<HI_U64>(reinterpret_cast<HI_UL>(vir_addr));
    return true;
}

bool MotionBackend::AllocBlob() {
    HI_U64 phy_addr = 0;
    HI_VOID *vir_addr = nullptr;
    const HI_U32 blob_size = static_cast<HI_U32>(sizeof(IVE_CCBLOB_S));
    const HI_S32 ret = HI_MPI_SYS_MmzAlloc(
        &phy_addr, &vir_addr, "LIVE_AI_MD_BLOB", nullptr, blob_size);
    if (ret != HI_SUCCESS || phy_addr == 0 || vir_addr == nullptr) {
        return false;
    }
    std::memset(vir_addr, 0, blob_size);
    blob_.mem.u64PhyAddr = phy_addr;
    blob_.mem.u64VirAddr =
        static_cast<HI_U64>(reinterpret_cast<HI_UL>(vir_addr));
    blob_.mem.u32Size = blob_size;
    blob_.vir_addr = vir_addr;
    return true;
}

void MotionBackend::InitAttr(uint32_t width, uint32_t height) {
    std::memset(&attr_, 0, sizeof(attr_));
    attr_.enAlgMode = MD_ALG_MODE_BG;
    attr_.enSadMode = IVE_SAD_MODE_MB_4X4;
    attr_.enSadOutCtrl = IVE_SAD_OUT_CTRL_THRESH;
    attr_.u32Width = width;
    attr_.u32Height = height;
    attr_.u16SadThr = kMotionSadThreshold;
    attr_.stAddCtrl.u0q16X = kMotionBackgroundBlend;
    attr_.stAddCtrl.u0q16Y = kMotionBackgroundBlend;
    attr_.stCclCtrl.enMode = IVE_CCL_MODE_4C;
    const HI_U8 window_size =
        static_cast<HI_U8>(1U << (2U + attr_.enSadMode));
    attr_.stCclCtrl.u16InitAreaThr = window_size * window_size;
    attr_.stCclCtrl.u16Step = window_size;
}

bool MotionBackend::CopyFrameLuma(const hisisdk::YuvFrame &frame,
                                  MotionImage *image) const {
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

bool MotionBackend::CanUseFrame(const hisisdk::YuvFrame &frame) const {
    const hisisdk::MppYuvFrameInfo &info = frame.mpp_info;
    return initialized_ && info.valid && frame.width != 0 &&
           frame.height != 0 && info.phy_addr[0] != 0 &&
           info.stride[0] >= frame.width &&
           info.pixel_format ==
               static_cast<int32_t>(PIXEL_FORMAT_YVU_SEMIPLANAR_420) &&
           info.compress_mode == static_cast<int32_t>(COMPRESS_MODE_NONE);
}

std::vector<AiDetection> MotionBackend::DecodeBlob(
    const AiModelConfig &config) {
    std::vector<AiDetection> detections;
    if (blob_.vir_addr == nullptr || attr_.u32Width == 0 ||
        attr_.u32Height == 0) {
        return detections;
    }
    const IVE_CCBLOB_S *blob =
        static_cast<const IVE_CCBLOB_S *>(blob_.vir_addr);

    std::vector<const IVE_REGION_S *> regions;
    regions.reserve(std::min<uint32_t>(IVE_MAX_REGION_NUM, config.max_results));
    HI_U16 area_threshold = 0;
    if (blob->u8RegionNum > config.max_results) {
        area_threshold = blob->u16CurAreaThr;
        while (RegionSize(*blob, area_threshold) > config.max_results) {
            if (area_threshold >
                std::numeric_limits<HI_U16>::max() -
                    kMotionAreaThresholdStep) {
                break;
            }
            area_threshold += kMotionAreaThresholdStep;
        }
    }
    for (uint32_t i = 0; i < IVE_MAX_REGION_NUM; ++i) {
        const IVE_REGION_S &region = blob->astRegion[i];
        if (region.u32Area > area_threshold && IsValidRegion(region)) {
            regions.push_back(&region);
        }
    }
    std::sort(regions.begin(), regions.end(),
              [](const IVE_REGION_S *lhs, const IVE_REGION_S *rhs) {
                  return lhs->u32Area > rhs->u32Area;
              });
    if (regions.size() > config.max_results) {
        regions.resize(config.max_results);
    }

    detections.reserve(regions.size());
    const float frame_area =
        static_cast<float>(attr_.u32Width) * static_cast<float>(attr_.u32Height);
    for (const IVE_REGION_S *region : regions) {
        AiDetection detection;
        detection.label = "motion";
        const float area_ratio = static_cast<float>(region->u32Area) / frame_area;
        detection.confidence = ClampFloat(0.5f + area_ratio * 8.0f,
                                          0.0f, 1.0f);
        detection.x =
            static_cast<float>(region->u16Left) / static_cast<float>(attr_.u32Width);
        detection.y =
            static_cast<float>(region->u16Top) / static_cast<float>(attr_.u32Height);
        detection.width =
            static_cast<float>(region->u16Right - region->u16Left + 1U) /
            static_cast<float>(attr_.u32Width);
        detection.height =
            static_cast<float>(region->u16Bottom - region->u16Top + 1U) /
            static_cast<float>(attr_.u32Height);
        if (detection.confidence >= config.confidence_threshold) {
            detections.push_back(detection);
        }
    }
    return detections;
}

uint32_t MotionBackend::RegionSize(const IVE_CCBLOB_S &blob,
                                   HI_U16 area_threshold) const {
    uint32_t region_size = 0;
    for (uint32_t i = 0; i < IVE_MAX_REGION_NUM; ++i) {
        if (blob.astRegion[i].u32Area > area_threshold) {
            ++region_size;
        }
    }
    return region_size;
}

bool MotionBackend::IsValidRegion(const IVE_REGION_S &region) const {
    return region.u32Area != 0 && region.u16Right >= region.u16Left &&
           region.u16Bottom >= region.u16Top &&
           region.u16Right < attr_.u32Width &&
           region.u16Bottom < attr_.u32Height;
}
#endif

}  // namespace ai_internal
}  // namespace live_stream
