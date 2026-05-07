#include "osd_region.h"

#include "hisisdk/hisi_sdk.h"

namespace live_stream {
namespace osd_internal {
namespace {

constexpr int32_t kOverlayMinHandle = 0;
constexpr int32_t kOverlayExMinHandle = 20;
constexpr int32_t kCoverMinHandle = 40;
constexpr int32_t kCoverExMinHandle = 60;
constexpr int32_t kMosaicMinHandle = 80;

bool IsValidSize(const OsdSize& size) {
    return size.width > 0 && size.height > 0;
}

bool IsAligned(uint32_t value, uint32_t alignment) {
    return alignment == 0 || value % alignment == 0;
}

bool IsAlignedPoint(const OsdPoint& point, int32_t alignment_x,
                    int32_t alignment_y) {
    return point.x >= 0 && point.y >= 0 &&
           point.x % alignment_x == 0 && point.y % alignment_y == 0;
}

bool IsSupportedTarget(OsdRegionType type, MppModule module) {
    switch (type) {
        case OsdRegionType::kOverlay:
            return module == MppModule::kVenc;
        case OsdRegionType::kOverlayEx:
            return module == MppModule::kVi || module == MppModule::kVpss ||
                   module == MppModule::kVo;
        case OsdRegionType::kCover:
            return module == MppModule::kVpss;
        case OsdRegionType::kCoverEx:
            return module == MppModule::kVi || module == MppModule::kVpss ||
                   module == MppModule::kVo;
        case OsdRegionType::kMosaic:
            return module == MppModule::kVpss;
    }
    return false;
}

hisisdk::RegionType ToHisiRegionType(OsdRegionType type) {
    switch (type) {
        case OsdRegionType::kOverlay:
            return hisisdk::RegionType::kOverlay;
        case OsdRegionType::kOverlayEx:
            return hisisdk::RegionType::kOverlayEx;
        case OsdRegionType::kCover:
            return hisisdk::RegionType::kCover;
        case OsdRegionType::kCoverEx:
            return hisisdk::RegionType::kCoverEx;
        case OsdRegionType::kMosaic:
            return hisisdk::RegionType::kMosaic;
    }
    return hisisdk::RegionType::kOverlay;
}

hisisdk::PixelFormat ToHisiPixelFormat(OsdPixelFormat format) {
    switch (format) {
        case OsdPixelFormat::kArgb1555:
            return hisisdk::PixelFormat::kArgb1555;
        case OsdPixelFormat::kArgb4444:
            return hisisdk::PixelFormat::kArgb4444;
        case OsdPixelFormat::kArgb8888:
            return hisisdk::PixelFormat::kArgb8888;
        case OsdPixelFormat::kArgb2Bpp:
            return hisisdk::PixelFormat::kArgb2Bpp;
    }
    return hisisdk::PixelFormat::kArgb1555;
}

hisisdk::RegionConfig ToHisiRegionConfig(const OsdRegionConfig& config) {
    hisisdk::RegionConfig hisi_config;
    hisi_config.type = ToHisiRegionType(config.type);
    hisi_config.pixel_format = ToHisiPixelFormat(config.pixel_format);
    hisi_config.size =
        hisisdk::Size{config.size.width, config.size.height};
    hisi_config.position =
        hisisdk::Point{config.position.x, config.position.y};
    hisi_config.background_color = config.background_color;
    hisi_config.foreground_alpha = config.foreground_alpha;
    hisi_config.background_alpha = config.background_alpha;
    hisi_config.visible = config.visible;
    hisi_config.target = config.target;
    return hisi_config;
}

hisisdk::Bitmap ToHisiBitmap(const OsdBitmap& bitmap) {
    hisisdk::Bitmap hisi_bitmap;
    hisi_bitmap.data = bitmap.data;
    hisi_bitmap.size = bitmap.size;
    hisi_bitmap.stride = bitmap.stride;
    hisi_bitmap.dimensions =
        hisisdk::Size{bitmap.dimensions.width, bitmap.dimensions.height};
    hisi_bitmap.pixel_format = ToHisiPixelFormat(bitmap.pixel_format);
    return hisi_bitmap;
}

}  // namespace

HostOsdMppAdapter::HostOsdMppAdapter(hisisdk::IHisiSdk* sdk)
    : sdk_(sdk != nullptr ? sdk : &hisisdk::DefaultSdk()) {}

bool IsValidChannel(const MppChannel& channel) {
    return channel.device >= 0 && channel.channel >= 0;
}

bool IsValidRegionConfig(const OsdRegionConfig& config) {
    if (!IsValidSize(config.size) || !IsValidChannel(config.target) ||
        !IsSupportedTarget(config.type, config.target.module)) {
        return false;
    }
    switch (config.type) {
        case OsdRegionType::kOverlay:
        case OsdRegionType::kOverlayEx:
            return IsAligned(config.size.width, 2) &&
                   IsAligned(config.size.height, 2) &&
                   IsAlignedPoint(config.position, 2, 2);
        case OsdRegionType::kMosaic:
            return config.size.width >= 32 && config.size.height >= 32 &&
                   IsAligned(config.size.width, 4) &&
                   IsAligned(config.size.height, 4) &&
                   IsAlignedPoint(config.position, 4, 2);
        case OsdRegionType::kCover:
        case OsdRegionType::kCoverEx:
            return true;
    }
    return false;
}

bool IsValidBitmap(const OsdBitmap& bitmap) {
    const uint64_t min_size =
        static_cast<uint64_t>(bitmap.stride) * bitmap.dimensions.height;
    return bitmap.data != nullptr && bitmap.size > 0 && bitmap.stride > 0 &&
           IsValidSize(bitmap.dimensions) && min_size <= bitmap.size;
}

int32_t MinHandle(OsdRegionType type) {
    switch (type) {
        case OsdRegionType::kOverlay:
            return kOverlayMinHandle;
        case OsdRegionType::kOverlayEx:
            return kOverlayExMinHandle;
        case OsdRegionType::kCover:
            return kCoverMinHandle;
        case OsdRegionType::kCoverEx:
            return kCoverExMinHandle;
        case OsdRegionType::kMosaic:
            return kMosaicMinHandle;
    }
    return -1;
}

bool HostOsdMppAdapter::Create(
    int32_t handle, const OsdRegionConfig& config) {
    if (handle < 0 || !IsValidRegionConfig(config)) {
        return false;
    }
    return sdk_->CreateRegion(handle, ToHisiRegionConfig(config));
}

bool HostOsdMppAdapter::Attach(
    int32_t handle, const OsdRegionConfig& config) {
    if (handle < 0 || !IsValidRegionConfig(config)) {
        return false;
    }
    return sdk_->AttachRegion(handle, ToHisiRegionConfig(config));
}

bool HostOsdMppAdapter::Detach(
    int32_t handle, const OsdRegionConfig& config) {
    if (handle < 0) {
        return false;
    }
    return sdk_->DetachRegion(handle, ToHisiRegionConfig(config));
}

bool HostOsdMppAdapter::SetDisplay(
    int32_t handle, const OsdRegionConfig& config) {
    if (handle < 0 || !IsValidRegionConfig(config)) {
        return false;
    }
    return sdk_->SetRegionDisplay(handle, ToHisiRegionConfig(config));
}

bool HostOsdMppAdapter::UpdateBitmap(
    int32_t handle, const OsdBitmap& bitmap) {
    if (handle < 0 || !IsValidBitmap(bitmap)) {
        return false;
    }
    return sdk_->SetRegionBitmap(handle, ToHisiBitmap(bitmap));
}

void HostOsdMppAdapter::Destroy(int32_t handle) {
    sdk_->DestroyRegion(handle);
}

}  // namespace osd_internal
}  // namespace live_stream
