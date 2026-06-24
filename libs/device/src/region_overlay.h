#ifndef LIVE_STREAM_DEVICE_SRC_REGION_OVERLAY_H_
#define LIVE_STREAM_DEVICE_SRC_REGION_OVERLAY_H_

#include "config.h"
#include "device.h"
#include "hisisdk/hisi_sdk.h"
#include "media/mpp_types.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace live_stream {
namespace device_internal {

struct RegionOverlayOptions {
    IConfig *config = nullptr;
    MediaChannels media_channels;
    hisisdk::IHisiSdk *sdk = nullptr;
};

enum class RegionOverlayState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

struct TextBitmap {
    std::vector<uint8_t> pixels;
    RegionSize size;
    uint32_t stride = 0;
};

struct PrivacyMask {
    bool enabled = false;
    RegionPoint position;
    RegionSize size;
    uint32_t color = 0x000000;
};

struct PrivacyMasks {
    static constexpr uint32_t kSlotSize = 4;
    PrivacyMask main[kSlotSize];
    PrivacyMask sub[kSlotSize];
};

struct ParsedOverlayConfig {
    bool enabled = false;
    bool timestamp_enabled = false;
    bool device_name_enabled = false;
    std::string timestamp_format;
    std::string device_name;
    uint32_t font_size = 16;
    uint32_t font_color = 0xffffff;
    bool background = true;
    RegionPoint timestamp_position;
    RegionPoint device_name_position;
    PrivacyMasks privacy_masks;
};

constexpr uint32_t kMaxRegions = 16;
constexpr uint32_t kMinFontSize = 8;
constexpr uint32_t kMaxFontSize = 72;

bool ParseHexColor(const std::string &text, uint32_t *color);
bool IsValidChannel(const MppChannel &channel);
bool IsValidRegionConfig(const RegionConfig &config);
bool IsValidBitmap(const RegionBitmap &bitmap);
int32_t MinHandle(RegionType type);
RegionBitmap BuildRegionBitmap(const TextBitmap &text_bitmap);
hisisdk::RegionConfig BuildSdkRegionConfig(const RegionConfig &config);
hisisdk::Bitmap BuildSdkBitmap(const RegionBitmap &bitmap);
std::string RegionTargetSuffix(const MppChannel &channel);
bool ParseTextOverlayConfig(const ConfigJson &value,
                            ParsedOverlayConfig *config);
bool ParsePrivacyMasksConfig(const ConfigJson &value,
                             const MediaChannels &channels,
                             PrivacyMasks *masks);

class RegionOverlay {
public:
    RegionOverlay();
    explicit RegionOverlay(const RegionOverlayOptions &options);
    ~RegionOverlay();

    bool Start();
    void Stop();
    bool Prepare();
    void Release();
    bool BindMedia(const MediaChannels &channels);
    OverlayInfo GetInfo() const;
    bool VerifyConfig(const ConfigJson &value) const;
    bool ApplyConfig(const ConfigJson &value);
    bool ApplyTextOverlay(const ParsedOverlayConfig &config);
    bool UpdateTimestampLocked();
    bool ApplyPrivacyMasks(const PrivacyMasks &masks);

    struct RegionRecord {
        RegionId id;
        int32_t mpp_handle = -1;
        std::string name;
        RegionConfig config;
        bool created = false;
        bool attached = false;
        bool has_bitmap = false;
    };

    RegionRecord *Find(RegionId id);
    const RegionRecord *Find(RegionId id) const;
    RegionRecord *FindByName(const std::string &name);
    int32_t AllocateHandle(RegionType type) const;
    void DetachAll();
    void DestroyAll();
    void DestroyRegionByPrefix(const std::string &prefix);
    MediaChannels ActiveChannels() const;
    void RefreshMediaChannels();
    std::vector<MppChannel> OverlayTargets() const;
    bool UpsertBitmapRegion(const std::string &name,
                            const RegionConfig &config,
                            const TextBitmap &text_bitmap);
    bool UpsertDisplayRegion(const std::string &name,
                             const RegionConfig &config);
    void StartRefreshThreadLocked();
    void StopRefreshThread();

    RegionOverlayOptions options;
    hisisdk::IHisiSdk *sdk = nullptr;
    RegionOverlayState state = RegionOverlayState::kCreated;
    bool media_bound = false;
    MediaChannels media_channels;
    uint32_t next_id = 1;
    std::vector<RegionRecord> regions;
    OverlayInfo stats;
    mutable std::mutex mutex;
    std::condition_variable refresh_condition;
    std::thread refresh_thread;
    ParsedOverlayConfig active_config;
    bool refresh_running = false;
    bool config_attached = false;
};

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_REGION_OVERLAY_H_
