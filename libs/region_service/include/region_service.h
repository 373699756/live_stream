#ifndef LIVE_STREAM_REGION_SERVICE_H_
#define LIVE_STREAM_REGION_SERVICE_H_

#include "media/mpp_types.h"

#include <cstdint>

namespace live_stream {

class IConfigService;
class IMediaService;
class RegionServiceImpl;

namespace hisisdk {
class IHisiSdk;
}  // namespace hisisdk

enum class RegionType {
    kOverlay = 0,
    kOverlayEx,
    kCover,
    kCoverEx,
    kMosaic,
};

enum class RegionPixelFormat {
    kArgb1555 = 0,
    kArgb4444,
    kArgb8888,
    kArgb2Bpp,
};

struct RegionPoint {
    int32_t x = 0;
    int32_t y = 0;
};

struct RegionSize {
    uint32_t width = 200;
    uint32_t height = 200;
};

struct RegionBitmap {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t stride = 0;
    RegionSize dimensions;
    RegionPixelFormat pixel_format = RegionPixelFormat::kArgb1555;
};

struct RegionConfig {
    RegionType type = RegionType::kOverlay;
    RegionPixelFormat pixel_format = RegionPixelFormat::kArgb1555;
    RegionSize size;
    RegionPoint position;
    uint32_t background_color = 0x00ff00ff;
    uint32_t foreground_alpha = 128;
    uint32_t background_alpha = 128;
    bool visible = true;
    MppChannel target;
};

struct RegionId {
    uint32_t value = 0;
};

struct RegionServiceOptions {
    IConfigService* config_service = nullptr;
    IMediaService* media_service = nullptr;
    MediaChannels media_channels;
    hisisdk::IHisiSdk* sdk = nullptr;
};

struct RegionServiceStats {
    uint64_t config_apply_count = 0;
    uint64_t config_apply_failed_count = 0;
    uint64_t bitmap_update_count = 0;
    uint32_t region_count = 0;
};

class RegionService {
public:
    RegionService();
    explicit RegionService(const RegionServiceOptions& options);
    ~RegionService();

    bool Start();
    void Stop();

    static const char* StaticName();

    bool BindMedia(const MediaChannels& channels);
    RegionId CreateRegion(const RegionConfig& config);
    bool Attach(RegionId id);
    bool Detach(RegionId id);
    bool SetVisible(RegionId id, bool visible);
    bool UpdateBitmap(RegionId id, const RegionBitmap& bitmap);
    bool DestroyRegion(RegionId id);
    uint32_t RegionCount() const;
    RegionServiceStats GetStats() const;

private:
    RegionServiceImpl* impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_REGION_SERVICE_H_
