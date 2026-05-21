#ifndef LIVE_STREAM_OSD_SERVICE_H_
#define LIVE_STREAM_OSD_SERVICE_H_

#include "media/mpp_types.h"

#include <cstdint>

namespace live_stream {

class IConfigService;
class IMediaService;

namespace hisisdk {
class IHisiSdk;
}  // namespace hisisdk

enum class OsdRegionType {
    kOverlay = 0,
    kOverlayEx,
    kCover,
    kCoverEx,
    kMosaic,
};

enum class OsdPixelFormat {
    kArgb1555 = 0,
    kArgb4444,
    kArgb8888,
    kArgb2Bpp,
};

struct OsdPoint {
    int32_t x = 0;
    int32_t y = 0;
};

struct OsdSize {
    uint32_t width = 200;
    uint32_t height = 200;
};

struct OsdBitmap {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t stride = 0;
    OsdSize dimensions;
    OsdPixelFormat pixel_format = OsdPixelFormat::kArgb1555;
};

struct OsdRegionConfig {
    OsdRegionType type = OsdRegionType::kOverlay;
    OsdPixelFormat pixel_format = OsdPixelFormat::kArgb1555;
    OsdSize size;
    OsdPoint position;
    uint32_t background_color = 0x00ff00ff;
    uint32_t foreground_alpha = 128;
    uint32_t background_alpha = 128;
    bool visible = true;
    MppChannel target;
};

struct OsdRegionId {
    uint32_t value = 0;
};

struct OsdServiceOptions {
    IConfigService* config_service = nullptr;
    IMediaService* media_service = nullptr;
    MediaChannels media_channels;
    hisisdk::IHisiSdk* sdk = nullptr;
};

struct OsdServiceStats {
    uint64_t config_apply_count = 0;
    uint64_t config_apply_failed_count = 0;
    uint64_t bitmap_update_count = 0;
    uint32_t region_count = 0;
};

class OsdService {
public:
    OsdService();
    explicit OsdService(const OsdServiceOptions& options);
    ~OsdService();

    bool Start();
    void Stop();

    static const char* StaticName();

    bool BindMedia(const MediaChannels& channels);
    OsdRegionId CreateRegion(const OsdRegionConfig& config);
    bool Attach(OsdRegionId id);
    bool Detach(OsdRegionId id);
    bool SetVisible(OsdRegionId id, bool visible);
    bool UpdateBitmap(OsdRegionId id, const OsdBitmap& bitmap);
    bool DestroyRegion(OsdRegionId id);
    uint32_t RegionCount() const;
    OsdServiceStats GetStats() const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_OSD_SERVICE_H_
