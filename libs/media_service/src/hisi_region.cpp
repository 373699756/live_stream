#include "hisi_sdk_default.h"

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
extern "C" {
#include "hi_comm_region.h"
#include "mpi_region.h"
}
#endif

namespace live_stream {
namespace hisisdk {
namespace {

bool IsValidSize(const Size& size) {
    return size.width > 0 && size.height > 0;
}

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
bool FromHiStatus(int32_t status) { return status == HI_SUCCESS; }

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

MPP_CHN_S ToHiChannel(const MppChannel& channel) {
    MPP_CHN_S hi_channel{};
    hi_channel.enModId = ToHiModule(channel.module);
    hi_channel.s32DevId = channel.device;
    hi_channel.s32ChnId = channel.channel;
    return hi_channel;
}

void FillRegionChannelAttr(const RegionConfig& config,
                           RGN_CHN_ATTR_S* attr) {
    attr->bShow = config.visible ? HI_TRUE : HI_FALSE;
    attr->enType = ToHiRegionType(config.type);
    switch (config.type) {
        case RegionType::kOverlay:
            attr->unChnAttr.stOverlayChn.stPoint.s32X = config.position.x;
            attr->unChnAttr.stOverlayChn.stPoint.s32Y = config.position.y;
            attr->unChnAttr.stOverlayChn.u32FgAlpha = config.foreground_alpha;
            attr->unChnAttr.stOverlayChn.u32BgAlpha = config.background_alpha;
            attr->unChnAttr.stOverlayChn.u32Layer = 0;
            break;
        case RegionType::kOverlayEx:
            attr->unChnAttr.stOverlayExChn.stPoint.s32X = config.position.x;
            attr->unChnAttr.stOverlayExChn.stPoint.s32Y = config.position.y;
            attr->unChnAttr.stOverlayExChn.u32FgAlpha =
                config.foreground_alpha;
            attr->unChnAttr.stOverlayExChn.u32BgAlpha =
                config.background_alpha;
            attr->unChnAttr.stOverlayExChn.u32Layer = 0;
            break;
        case RegionType::kCover:
            attr->unChnAttr.stCoverChn.enCoverType = AREA_RECT;
            attr->unChnAttr.stCoverChn.stRect.s32X = config.position.x;
            attr->unChnAttr.stCoverChn.stRect.s32Y = config.position.y;
            attr->unChnAttr.stCoverChn.stRect.u32Width = config.size.width;
            attr->unChnAttr.stCoverChn.stRect.u32Height = config.size.height;
            attr->unChnAttr.stCoverChn.u32Color = config.background_color;
            break;
        case RegionType::kCoverEx:
            attr->unChnAttr.stCoverExChn.enCoverType = AREA_RECT;
            attr->unChnAttr.stCoverExChn.stRect.s32X = config.position.x;
            attr->unChnAttr.stCoverExChn.stRect.s32Y = config.position.y;
            attr->unChnAttr.stCoverExChn.stRect.u32Width = config.size.width;
            attr->unChnAttr.stCoverExChn.stRect.u32Height =
                config.size.height;
            attr->unChnAttr.stCoverExChn.u32Color = config.background_color;
            break;
        case RegionType::kMosaic:
            attr->unChnAttr.stMosaicChn.stRect.s32X = config.position.x;
            attr->unChnAttr.stMosaicChn.stRect.s32Y = config.position.y;
            attr->unChnAttr.stMosaicChn.stRect.u32Width = config.size.width;
            attr->unChnAttr.stMosaicChn.stRect.u32Height = config.size.height;
            attr->unChnAttr.stMosaicChn.enBlkSize = MOSAIC_BLK_SIZE_16;
            break;
    }
}
#endif

}  // namespace

bool DefaultHisiSdk::CreateRegion(int32_t handle,
                                  const RegionConfig& config) {
    if (handle < 0 || !IsValidSize(config.size)) {
        return false;
    }
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    RGN_ATTR_S attr{};
    attr.enType = ToHiRegionType(config.type);
    if (config.type == RegionType::kOverlay) {
        attr.unAttr.stOverlay.enPixelFmt =
            ToHiPixelFormat(config.pixel_format);
        attr.unAttr.stOverlay.u32BgColor = config.background_color;
        attr.unAttr.stOverlay.stSize.u32Width = config.size.width;
        attr.unAttr.stOverlay.stSize.u32Height = config.size.height;
        attr.unAttr.stOverlay.u32CanvasNum = 2;
    } else if (config.type == RegionType::kOverlayEx) {
        attr.unAttr.stOverlayEx.enPixelFmt =
            ToHiPixelFormat(config.pixel_format);
        attr.unAttr.stOverlayEx.u32BgColor = config.background_color;
        attr.unAttr.stOverlayEx.stSize.u32Width = config.size.width;
        attr.unAttr.stOverlayEx.stSize.u32Height = config.size.height;
        attr.unAttr.stOverlayEx.u32CanvasNum = 2;
    }
    return FromHiStatus(HI_MPI_RGN_Create(handle, &attr));
#else
    return true;
#endif
}

bool DefaultHisiSdk::AttachRegion(int32_t handle,
                                  const RegionConfig& config) {
    (void)config;
    if (handle < 0) {
        return false;
    }
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    MPP_CHN_S channel = ToHiChannel(config.target);
    RGN_CHN_ATTR_S attr{};
    FillRegionChannelAttr(config, &attr);
    return FromHiStatus(HI_MPI_RGN_AttachToChn(handle, &channel, &attr));
#else
    return true;
#endif
}

bool DefaultHisiSdk::DetachRegion(int32_t handle,
                                  const RegionConfig& config) {
    (void)config;
    if (handle < 0) {
        return false;
    }
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    MPP_CHN_S channel = ToHiChannel(config.target);
    return FromHiStatus(HI_MPI_RGN_DetachFromChn(handle, &channel));
#else
    return true;
#endif
}

bool DefaultHisiSdk::SetRegionDisplay(
    int32_t handle,
    const RegionConfig& config) {
    (void)config;
    if (handle < 0) {
        return false;
    }
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    MPP_CHN_S channel = ToHiChannel(config.target);
    RGN_CHN_ATTR_S attr{};
    FillRegionChannelAttr(config, &attr);
    return FromHiStatus(HI_MPI_RGN_SetDisplayAttr(handle, &channel, &attr));
#else
    return true;
#endif
}

bool DefaultHisiSdk::SetRegionBitmap(int32_t handle,
                                     const Bitmap& bitmap) {
    if (handle < 0 || bitmap.data == nullptr || bitmap.size == 0) {
        return false;
    }
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    BITMAP_S hi_bitmap{};
    hi_bitmap.enPixelFormat = ToHiPixelFormat(bitmap.pixel_format);
    hi_bitmap.u32Width = bitmap.dimensions.width;
    hi_bitmap.u32Height = bitmap.dimensions.height;
    hi_bitmap.pData = const_cast<uint8_t*>(bitmap.data);
    return FromHiStatus(HI_MPI_RGN_SetBitMap(handle, &hi_bitmap));
#else
    return true;
#endif
}

void DefaultHisiSdk::DestroyRegion(int32_t handle) {
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    if (handle >= 0) {
        (void)HI_MPI_RGN_Destroy(handle);
    }
#else
    (void)handle;
#endif
}

}  // namespace hisisdk
}  // namespace live_stream
