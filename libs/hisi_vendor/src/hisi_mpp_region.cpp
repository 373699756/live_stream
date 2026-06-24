#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

namespace live_stream {
namespace hisisdk {

namespace {

constexpr uint32_t kOverlayLayerSize = 8;
constexpr uint32_t kOverlayExLayerSize = 16;
constexpr uint32_t kCoverLayerSize = 8;
constexpr uint32_t kMosaicLayerSize = 4;

// ─── Pixel format conversion ───────────────────────────────────
PIXEL_FORMAT_E ToHiPixelFormat(PixelFormat format) {
    switch (format) {
        case PixelFormat::kArgb1555:
            return PIXEL_FORMAT_ARGB_1555;
        case PixelFormat::kArgb4444:
            return PIXEL_FORMAT_ARGB_4444;
        case PixelFormat::kArgb8888:
            return PIXEL_FORMAT_ARGB_8888;
        case PixelFormat::kArgb2Bpp:
            return PIXEL_FORMAT_ARGB_2BPP;
    }
    return PIXEL_FORMAT_ARGB_1555;
}

// ─── Region type conversion ────────────────────────────────────
RGN_TYPE_E ToHiRegionType(RegionType type) {
    switch (type) {
        case RegionType::kOverlay:
            return OVERLAY_RGN;
        case RegionType::kOverlayEx:
            return OVERLAYEX_RGN;
        case RegionType::kCover:
            return COVER_RGN;
        case RegionType::kCoverEx:
            return COVEREX_RGN;
        case RegionType::kMosaic:
            return MOSAIC_RGN;
    }
    return OVERLAY_RGN;
}

// ─── Module ID conversion ──────────────────────────────────────
MOD_ID_E ToHiModule(MppModule module) {
    switch (module) {
        case MppModule::kVi:
            return HI_ID_VI;
        case MppModule::kVpss:
            return HI_ID_VPSS;
        case MppModule::kVenc:
            return HI_ID_VENC;
        case MppModule::kVo:
            return HI_ID_VO;
    }
    return HI_ID_VENC;
}

// ─── Channel conversion ────────────────────────────────────────
MPP_CHN_S ToHiChannel(const MppChannel& channel) {
    MPP_CHN_S hi_ch{};
    hi_ch.enModId = ToHiModule(channel.module);
    hi_ch.s32DevId = channel.device;
    hi_ch.s32ChnId = channel.channel;
    return hi_ch;
}

uint32_t RegionLayer(int32_t handle, uint32_t layer_size) {
    if (handle < 0 || layer_size == 0) {
        return 0;
    }
    return static_cast<uint32_t>(handle) % layer_size;
}

// ─── Fill channel display attribute ────────────────────────────
void FillChannelAttr(int32_t handle, const RegionConfig& config,
                     RGN_CHN_ATTR_S* attr) {
    if (attr == nullptr) {
        return;
    }
    attr->bShow = config.visible ? HI_TRUE : HI_FALSE;
    attr->enType = ToHiRegionType(config.type);
    switch (config.type) {
        case RegionType::kOverlay:
            attr->unChnAttr.stOverlayChn.stPoint.s32X = config.position.x;
            attr->unChnAttr.stOverlayChn.stPoint.s32Y = config.position.y;
            attr->unChnAttr.stOverlayChn.u32FgAlpha = config.foreground_alpha;
            attr->unChnAttr.stOverlayChn.u32BgAlpha = config.background_alpha;
            attr->unChnAttr.stOverlayChn.u32Layer =
                RegionLayer(handle, kOverlayLayerSize);
            attr->unChnAttr.stOverlayChn.stQpInfo.bQpDisable = HI_TRUE;
            attr->unChnAttr.stOverlayChn.stInvertColor.bInvColEn = HI_FALSE;
            attr->unChnAttr.stOverlayChn.enAttachDest = ATTACH_JPEG_MAIN;
            break;
        case RegionType::kOverlayEx:
            attr->unChnAttr.stOverlayExChn.stPoint.s32X = config.position.x;
            attr->unChnAttr.stOverlayExChn.stPoint.s32Y = config.position.y;
            attr->unChnAttr.stOverlayExChn.u32FgAlpha = config.foreground_alpha;
            attr->unChnAttr.stOverlayExChn.u32BgAlpha = config.background_alpha;
            attr->unChnAttr.stOverlayExChn.u32Layer =
                RegionLayer(handle, kOverlayExLayerSize);
            break;
        case RegionType::kCover:
            attr->unChnAttr.stCoverChn.enCoverType = AREA_RECT;
            attr->unChnAttr.stCoverChn.stRect.s32X = config.position.x;
            attr->unChnAttr.stCoverChn.stRect.s32Y = config.position.y;
            attr->unChnAttr.stCoverChn.stRect.u32Width = config.size.width;
            attr->unChnAttr.stCoverChn.stRect.u32Height = config.size.height;
            attr->unChnAttr.stCoverChn.u32Color = config.background_color;
            attr->unChnAttr.stCoverChn.u32Layer =
                RegionLayer(handle, kCoverLayerSize);
            attr->unChnAttr.stCoverChn.enCoordinate = RGN_ABS_COOR;
            break;
        case RegionType::kCoverEx:
            attr->unChnAttr.stCoverExChn.enCoverType = AREA_RECT;
            attr->unChnAttr.stCoverExChn.stRect.s32X = config.position.x;
            attr->unChnAttr.stCoverExChn.stRect.s32Y = config.position.y;
            attr->unChnAttr.stCoverExChn.stRect.u32Width = config.size.width;
            attr->unChnAttr.stCoverExChn.stRect.u32Height = config.size.height;
            attr->unChnAttr.stCoverExChn.u32Color = config.background_color;
            attr->unChnAttr.stCoverExChn.u32Layer =
                RegionLayer(handle, kCoverLayerSize);
            break;
        case RegionType::kMosaic:
            attr->unChnAttr.stMosaicChn.stRect.s32X = config.position.x;
            attr->unChnAttr.stMosaicChn.stRect.s32Y = config.position.y;
            attr->unChnAttr.stMosaicChn.stRect.u32Width = config.size.width;
            attr->unChnAttr.stMosaicChn.stRect.u32Height = config.size.height;
            attr->unChnAttr.stMosaicChn.enBlkSize = MOSAIC_BLK_SIZE_16;
            attr->unChnAttr.stMosaicChn.u32Layer =
                RegionLayer(handle, kMosaicLayerSize);
            break;
    }
}

}  // anonymous namespace

// ====================================================================
// CreateRegion
// ====================================================================
bool MppHisiSdk::CreateRegion(int32_t handle, const RegionConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (handle < 0 || config.size.width == 0 || config.size.height == 0) {
        Error(
            "hisi_vendor",
            "invalid region create handle=%d type=%d size=%ux%u "
            "target=%d:%d:%d",
            handle, static_cast<int>(config.type), config.size.width,
            config.size.height, static_cast<int>(config.target.module),
            config.target.device, config.target.channel);
        return false;
    }

    RGN_ATTR_S attr{};
    attr.enType = ToHiRegionType(config.type);

    if (config.type == RegionType::kOverlay) {
        attr.unAttr.stOverlay.enPixelFmt = ToHiPixelFormat(config.pixel_format);
        attr.unAttr.stOverlay.u32BgColor = config.background_color;
        attr.unAttr.stOverlay.stSize.u32Width = config.size.width;
        attr.unAttr.stOverlay.stSize.u32Height = config.size.height;
        attr.unAttr.stOverlay.u32CanvasNum = 2;
    } else if (config.type == RegionType::kOverlayEx) {
        attr.unAttr.stOverlayEx.enPixelFmt = ToHiPixelFormat(config.pixel_format);
        attr.unAttr.stOverlayEx.u32BgColor = config.background_color;
        attr.unAttr.stOverlayEx.stSize.u32Width = config.size.width;
        attr.unAttr.stOverlayEx.stSize.u32Height = config.size.height;
        attr.unAttr.stOverlayEx.u32CanvasNum = 2;
    }

    const HI_S32 status = HI_MPI_RGN_Create(handle, &attr);
    if (status != HI_SUCCESS) {
        Error(
            "hisi_vendor",
            "HI_MPI_RGN_Create failed: 0x%08x handle=%d type=%d "
            "size=%ux%u target=%d:%d:%d",
            status, handle, static_cast<int>(config.type),
            config.size.width, config.size.height,
            static_cast<int>(config.target.module), config.target.device,
            config.target.channel);
        return false;
    }
    return true;
}

// ====================================================================
// AttachRegion
// ====================================================================
bool MppHisiSdk::AttachRegion(int32_t handle, const RegionConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (handle < 0) {
        Error("hisi_vendor", "invalid region attach handle=%d",
              handle);
        return false;
    }

    MPP_CHN_S channel = ToHiChannel(config.target);
    RGN_CHN_ATTR_S attr{};
    FillChannelAttr(handle, config, &attr);
    const HI_S32 status = HI_MPI_RGN_AttachToChn(handle, &channel, &attr);
    if (status != HI_SUCCESS) {
        Error(
            "hisi_vendor",
            "HI_MPI_RGN_AttachToChn failed: 0x%08x handle=%d type=%d "
            "target=%d:%d:%d x=%d y=%d width=%u height=%u visible=%d",
            status, handle, static_cast<int>(config.type),
            static_cast<int>(config.target.module), config.target.device,
            config.target.channel, config.position.x, config.position.y,
            config.size.width, config.size.height, config.visible ? 1 : 0);
        return false;
    }
    return true;
}

// ====================================================================
// DetachRegion
// ====================================================================
bool MppHisiSdk::DetachRegion(int32_t handle, const RegionConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (handle < 0) return false;

    MPP_CHN_S channel = ToHiChannel(config.target);
    const HI_S32 status = HI_MPI_RGN_DetachFromChn(handle, &channel);
    if (status != HI_SUCCESS) {
        Error(
            "hisi_vendor",
            "HI_MPI_RGN_DetachFromChn failed: 0x%08x handle=%d "
            "target=%d:%d:%d",
            status, handle, static_cast<int>(config.target.module),
            config.target.device, config.target.channel);
        return false;
    }
    return true;
}

// ====================================================================
// SetRegionDisplay
// ====================================================================
bool MppHisiSdk::SetRegionDisplay(int32_t handle, const RegionConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (handle < 0) {
        Error("hisi_vendor", "invalid region display handle=%d",
              handle);
        return false;
    }

    MPP_CHN_S channel = ToHiChannel(config.target);
    RGN_CHN_ATTR_S attr{};
    FillChannelAttr(handle, config, &attr);
    const HI_S32 status = HI_MPI_RGN_SetDisplayAttr(handle, &channel, &attr);
    if (status != HI_SUCCESS) {
        Error(
            "hisi_vendor",
            "HI_MPI_RGN_SetDisplayAttr failed: 0x%08x handle=%d type=%d "
            "target=%d:%d:%d x=%d y=%d width=%u height=%u visible=%d "
            "color=0x%06x",
            status, handle, static_cast<int>(config.type),
            static_cast<int>(config.target.module), config.target.device,
            config.target.channel, config.position.x, config.position.y,
            config.size.width, config.size.height, config.visible ? 1 : 0,
            config.background_color);
        return false;
    }
    return true;
}

// ====================================================================
// SetRegionBitmap
// ====================================================================
bool MppHisiSdk::SetRegionBitmap(int32_t handle, const Bitmap& bitmap) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (handle < 0 || bitmap.data == nullptr || bitmap.size == 0) {
        Error(
            "hisi_vendor",
            "invalid region bitmap handle=%d data=%p size=%u width=%u "
            "height=%u stride=%u",
            handle, bitmap.data, bitmap.size, bitmap.dimensions.width,
            bitmap.dimensions.height, bitmap.stride);
        return false;
    }

    BITMAP_S hi_bitmap{};
    hi_bitmap.enPixelFormat = ToHiPixelFormat(bitmap.pixel_format);
    hi_bitmap.u32Width = bitmap.dimensions.width;
    hi_bitmap.u32Height = bitmap.dimensions.height;
    hi_bitmap.pData = const_cast<uint8_t*>(bitmap.data);
    const HI_S32 status = HI_MPI_RGN_SetBitMap(handle, &hi_bitmap);
    if (status != HI_SUCCESS) {
        Error(
            "hisi_vendor",
            "HI_MPI_RGN_SetBitMap failed: 0x%08x handle=%d width=%u "
            "height=%u stride=%u size=%u",
            status, handle, bitmap.dimensions.width,
            bitmap.dimensions.height, bitmap.stride, bitmap.size);
        return false;
    }
    return true;
}

// ====================================================================
// DestroyRegion
// ====================================================================
void MppHisiSdk::DestroyRegion(int32_t handle) {
    std::lock_guard<std::recursive_mutex> lock(impl_->control_mutex_);
    if (handle < 0) return;

    (void)HI_MPI_RGN_Destroy(handle);
}

}  // namespace hisisdk
}  // namespace live_stream
