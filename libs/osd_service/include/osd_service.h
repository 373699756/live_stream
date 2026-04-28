#ifndef LIVE_STREAM_OSD_SERVICE_H_
#define LIVE_STREAM_OSD_SERVICE_H_

#include "infra/status.h"
#include "infra/service.h"
#include "media/mpp_types.h"

#include <cstdint>

namespace live_stream {

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

class OsdService : public infra::IService {
 public:
    OsdService();
    ~OsdService() override;

    infra::Status Init() override;
    infra::Status Start() override;
    void Stop() override;
    void Deinit() override;
    const char* Name() const override;

    static const char* StaticName();

    infra::Status BindMedia(const MediaChannels& channels);
    infra::Result<OsdRegionId> CreateRegion(const OsdRegionConfig& config);
    infra::Status Attach(OsdRegionId id);
    infra::Status Detach(OsdRegionId id);
    infra::Status SetVisible(OsdRegionId id, bool visible);
    infra::Status UpdateBitmap(OsdRegionId id, const OsdBitmap& bitmap);
    infra::Status DestroyRegion(OsdRegionId id);
    uint32_t RegionCount() const;

 private:
    struct Impl;
    Impl* impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_OSD_SERVICE_H_
