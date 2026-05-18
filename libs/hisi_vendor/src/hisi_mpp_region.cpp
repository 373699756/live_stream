#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

namespace live_stream {
namespace hisisdk {

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
namespace {

constexpr uint32_t kOverlayLayerCount = 8;
constexpr uint32_t kOverlayExLayerCount = 16;
constexpr uint32_t kCoverLayerCount = 8;
constexpr uint32_t kMosaicLayerCount = 4;

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

uint32_t RegionLayer(int32_t handle, uint32_t layer_count) {
    if (handle < 0 || layer_count == 0) {
        return 0;
    }
    return static_cast<uint32_t>(handle) % layer_count;
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
                RegionLayer(handle, kOverlayLayerCount);
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
                RegionLayer(handle, kOverlayExLayerCount);
            break;
        case RegionType::kCover:
            attr->unChnAttr.stCoverChn.enCoverType = AREA_RECT;
            attr->unChnAttr.stCoverChn.stRect.s32X = config.position.x;
            attr->unChnAttr.stCoverChn.stRect.s32Y = config.position.y;
            attr->unChnAttr.stCoverChn.stRect.u32Width = config.size.width;
            attr->unChnAttr.stCoverChn.stRect.u32Height = config.size.height;
            attr->unChnAttr.stCoverChn.u32Color = config.background_color;
            attr->unChnAttr.stCoverChn.u32Layer =
                RegionLayer(handle, kCoverLayerCount);
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
                RegionLayer(handle, kCoverLayerCount);
            break;
        case RegionType::kMosaic:
            attr->unChnAttr.stMosaicChn.stRect.s32X = config.position.x;
            attr->unChnAttr.stMosaicChn.stRect.s32Y = config.position.y;
            attr->unChnAttr.stMosaicChn.stRect.u32Width = config.size.width;
            attr->unChnAttr.stMosaicChn.stRect.u32Height = config.size.height;
            attr->unChnAttr.stMosaicChn.enBlkSize = MOSAIC_BLK_SIZE_16;
            attr->unChnAttr.stMosaicChn.u32Layer =
                RegionLayer(handle, kMosaicLayerCount);
            break;
    }
}

}  // anonymous namespace
#endif  // LIVE_STREAM_ENABLE_HISI_MPP

// ====================================================================
// CreateRegion
// ====================================================================
bool MppHisiSdk::CreateRegion(int32_t handle, const RegionConfig& config) {
    if (handle < 0 || config.size.width == 0 || config.size.height == 0) {
        return false;
    }

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
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

    return internal::HiOk(HI_MPI_RGN_Create(handle, &attr));
#else
    (void)handle;
    (void)config;
    return true;
#endif
}

// ====================================================================
// AttachRegion
// ====================================================================
bool MppHisiSdk::AttachRegion(int32_t handle, const RegionConfig& config) {
    if (handle < 0) return false;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    MPP_CHN_S channel = ToHiChannel(config.target);
    RGN_CHN_ATTR_S attr{};
    FillChannelAttr(handle, config, &attr);
    return internal::HiOk(HI_MPI_RGN_AttachToChn(handle, &channel, &attr));
#else
    (void)handle;
    (void)config;
    return true;
#endif
}

// ====================================================================
// DetachRegion
// ====================================================================
bool MppHisiSdk::DetachRegion(int32_t handle, const RegionConfig& config) {
    if (handle < 0) return false;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    MPP_CHN_S channel = ToHiChannel(config.target);
    return internal::HiOk(HI_MPI_RGN_DetachFromChn(handle, &channel));
#else
    (void)handle;
    (void)config;
    return true;
#endif
}

// ====================================================================
// SetRegionDisplay
// ====================================================================
bool MppHisiSdk::SetRegionDisplay(int32_t handle, const RegionConfig& config) {
    if (handle < 0) return false;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    MPP_CHN_S channel = ToHiChannel(config.target);
    RGN_CHN_ATTR_S attr{};
    FillChannelAttr(handle, config, &attr);
    return internal::HiOk(HI_MPI_RGN_SetDisplayAttr(handle, &channel, &attr));
#else
    (void)handle;
    (void)config;
    return true;
#endif
}

// ====================================================================
// SetRegionBitmap
// ====================================================================
bool MppHisiSdk::SetRegionBitmap(int32_t handle, const Bitmap& bitmap) {
    if (handle < 0 || bitmap.data == nullptr || bitmap.size == 0) {
        return false;
    }

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    BITMAP_S hi_bitmap{};
    hi_bitmap.enPixelFormat = ToHiPixelFormat(bitmap.pixel_format);
    hi_bitmap.u32Width = bitmap.dimensions.width;
    hi_bitmap.u32Height = bitmap.dimensions.height;
    hi_bitmap.pData = const_cast<uint8_t*>(bitmap.data);
    return internal::HiOk(HI_MPI_RGN_SetBitMap(handle, &hi_bitmap));
#else
    (void)handle;
    (void)bitmap;
    return true;
#endif
}

// ====================================================================
// DestroyRegion
// ====================================================================
void MppHisiSdk::DestroyRegion(int32_t handle) {
    if (handle < 0) return;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    (void)HI_MPI_RGN_Destroy(handle);
#else
    (void)handle;
#endif
}

}  // namespace hisisdk
}  // namespace live_stream
