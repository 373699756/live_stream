#include "osd_service.h"

#include "config_service.h"
#include "live_stream/json_utils.h"
#include "media_service.h"
#include "osd_region.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace live_stream {

namespace {

enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

enum class ConfigRegionKind {
    kTimestamp = 0,
    kDeviceName,
};

struct TextBitmap {
    std::vector<uint8_t> pixels;
    OsdSize size;
    uint32_t stride = 0;
};

struct ParsedOsdConfig {
    bool enabled = false;
    bool timestamp_enabled = false;
    bool device_name_enabled = false;
    std::string timestamp_format;
    std::string device_name;
    uint32_t font_size = 24;
    uint32_t font_color = 0xffffff;
    bool background = true;
    OsdPoint timestamp_position;
    OsdPoint device_name_position;
};

constexpr uint32_t kMinFontSize = 8;
constexpr uint32_t kMaxFontSize = 72;
constexpr uint32_t kGlyphWidth = 5;
constexpr uint32_t kGlyphHeight = 7;
constexpr uint32_t kGlyphSpacing = 1;
constexpr uint32_t kTextPaddingX = 4;
constexpr uint32_t kTextPaddingY = 3;

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) *
                                      alignment;
}

uint16_t Argb1555(uint32_t rgb, bool opaque) {
    if (!opaque) {
        return 0;
    }
    const uint16_t r = static_cast<uint16_t>((rgb >> 19) & 0x1f);
    const uint16_t g = static_cast<uint16_t>((rgb >> 11) & 0x1f);
    const uint16_t b = static_cast<uint16_t>((rgb >> 3) & 0x1f);
    return static_cast<uint16_t>(0x8000 | (r << 10) | (g << 5) | b);
}

bool ParseHexColor(const std::string &text, uint32_t *color) {
    if (color == nullptr || text.size() != 7 || text[0] != '#') {
        return false;
    }
    uint32_t parsed = 0;
    for (size_t i = 1; i < text.size(); ++i) {
        const char ch = text[i];
        uint32_t digit = 0;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<uint32_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = static_cast<uint32_t>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            digit = static_cast<uint32_t>(ch - 'A' + 10);
        } else {
            return false;
        }
        parsed = (parsed << 4) | digit;
    }
    *color = parsed;
    return true;
}

const uint8_t *GlyphRows(char ch) {
    static const uint8_t kBlank[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t kBox[7] = {31, 17, 17, 17, 17, 17, 31};
    static const uint8_t kGlyphs[][7] = {
        {14, 17, 19, 21, 25, 17, 14},  // 0
        {4, 12, 4, 4, 4, 4, 14},       // 1
        {14, 17, 1, 2, 4, 8, 31},      // 2
        {30, 1, 1, 14, 1, 1, 30},      // 3
        {2, 6, 10, 18, 31, 2, 2},      // 4
        {31, 16, 30, 1, 1, 17, 14},    // 5
        {6, 8, 16, 30, 17, 17, 14},    // 6
        {31, 1, 2, 4, 8, 8, 8},        // 7
        {14, 17, 17, 14, 17, 17, 14},  // 8
        {14, 17, 17, 15, 1, 2, 12},    // 9
        {14, 17, 17, 31, 17, 17, 17},  // A
        {30, 17, 17, 30, 17, 17, 30},  // B
        {14, 17, 16, 16, 16, 17, 14},  // C
        {30, 17, 17, 17, 17, 17, 30},  // D
        {31, 16, 16, 30, 16, 16, 31},  // E
        {31, 16, 16, 30, 16, 16, 16},  // F
        {14, 17, 16, 23, 17, 17, 14},  // G
        {17, 17, 17, 31, 17, 17, 17},  // H
        {14, 4, 4, 4, 4, 4, 14},       // I
        {1, 1, 1, 1, 17, 17, 14},      // J
        {17, 18, 20, 24, 20, 18, 17},  // K
        {16, 16, 16, 16, 16, 16, 31},  // L
        {17, 27, 21, 21, 17, 17, 17},  // M
        {17, 25, 21, 19, 17, 17, 17},  // N
        {14, 17, 17, 17, 17, 17, 14},  // O
        {30, 17, 17, 30, 16, 16, 16},  // P
        {14, 17, 17, 17, 21, 18, 13},  // Q
        {30, 17, 17, 30, 20, 18, 17},  // R
        {15, 16, 16, 14, 1, 1, 30},    // S
        {31, 4, 4, 4, 4, 4, 4},        // T
        {17, 17, 17, 17, 17, 17, 14},  // U
        {17, 17, 17, 17, 17, 10, 4},   // V
        {17, 17, 17, 21, 21, 21, 10},  // W
        {17, 17, 10, 4, 10, 17, 17},   // X
        {17, 17, 10, 4, 4, 4, 4},      // Y
        {31, 1, 2, 4, 8, 16, 31},      // Z
    };
    static const uint8_t kColon[7] = {0, 4, 4, 0, 4, 4, 0};
    static const uint8_t kDash[7] = {0, 0, 0, 31, 0, 0, 0};
    static const uint8_t kSlash[7] = {1, 1, 2, 4, 8, 16, 16};
    static const uint8_t kDot[7] = {0, 0, 0, 0, 0, 12, 12};
    static const uint8_t kUnderscore[7] = {0, 0, 0, 0, 0, 0, 31};
    if (ch == ' ') {
        return kBlank;
    }
    if (ch >= '0' && ch <= '9') {
        return kGlyphs[ch - '0'];
    }
    if (ch >= 'a' && ch <= 'z') {
        ch = static_cast<char>(ch - 'a' + 'A');
    }
    if (ch >= 'A' && ch <= 'Z') {
        return kGlyphs[10 + ch - 'A'];
    }
    if (ch == ':') {
        return kColon;
    }
    if (ch == '-') {
        return kDash;
    }
    if (ch == '/') {
        return kSlash;
    }
    if (ch == '.') {
        return kDot;
    }
    if (ch == '_') {
        return kUnderscore;
    }
    return kBox;
}

TextBitmap RenderTextBitmap(const std::string &text, uint32_t font_size,
                            uint32_t font_color, bool background) {
    TextBitmap bitmap;
    const uint32_t scale = std::max<uint32_t>(1, font_size / kGlyphHeight);
    const uint32_t glyph_width = kGlyphWidth * scale;
    const uint32_t glyph_height = kGlyphHeight * scale;
    const uint32_t spacing = kGlyphSpacing * scale;
    const uint32_t text_width =
        text.empty() ? glyph_width
                     : static_cast<uint32_t>(text.size()) *
                               (glyph_width + spacing) -
                           spacing;
    bitmap.size.width = AlignUp(text_width + kTextPaddingX * 2, 2);
    bitmap.size.height = AlignUp(glyph_height + kTextPaddingY * 2, 2);
    bitmap.stride = bitmap.size.width * 2;
    bitmap.pixels.assign(bitmap.stride * bitmap.size.height, 0);

    const uint16_t background_pixel = Argb1555(0x000000, background);
    const uint16_t foreground_pixel = Argb1555(font_color, true);
    uint16_t *pixels = reinterpret_cast<uint16_t *>(bitmap.pixels.data());
    if (background_pixel != 0) {
        std::fill(pixels, pixels + bitmap.size.width * bitmap.size.height,
                  background_pixel);
    }
    for (size_t index = 0; index < text.size(); ++index) {
        const uint8_t *rows = GlyphRows(text[index]);
        const uint32_t origin_x = kTextPaddingX +
                                  static_cast<uint32_t>(index) *
                                      (glyph_width + spacing);
        const uint32_t origin_y = kTextPaddingY;
        for (uint32_t gy = 0; gy < kGlyphHeight; ++gy) {
            for (uint32_t gx = 0; gx < kGlyphWidth; ++gx) {
                if ((rows[gy] & (1U << (kGlyphWidth - 1 - gx))) == 0) {
                    continue;
                }
                for (uint32_t sy = 0; sy < scale; ++sy) {
                    for (uint32_t sx = 0; sx < scale; ++sx) {
                        const uint32_t x = origin_x + gx * scale + sx;
                        const uint32_t y = origin_y + gy * scale + sy;
                        pixels[y * bitmap.size.width + x] = foreground_pixel;
                    }
                }
            }
        }
    }
    return bitmap;
}

OsdBitmap ToOsdBitmap(const TextBitmap &text_bitmap) {
    OsdBitmap bitmap;
    bitmap.data = text_bitmap.pixels.data();
    bitmap.size = static_cast<uint32_t>(text_bitmap.pixels.size());
    bitmap.stride = text_bitmap.stride;
    bitmap.dimensions = text_bitmap.size;
    bitmap.pixel_format = OsdPixelFormat::kArgb1555;
    return bitmap;
}

std::string FormatTimestamp(const std::string &format) {
    const std::time_t now = std::time(nullptr);
    std::tm time_info{};
    if (localtime_r(&now, &time_info) == nullptr) {
        return "";
    }
    char text[128] = {};
    const char *fmt = format.empty() ? "%Y-%m-%d %H:%M:%S" : format.c_str();
    if (std::strftime(text, sizeof(text), fmt, &time_info) == 0) {
        return "";
    }
    return std::string(text);
}

std::string TargetSuffix(const MppChannel &channel) {
    if (channel.module == MppModule::kVenc && channel.channel == 0) {
        return "main";
    }
    if (channel.module == MppModule::kVenc && channel.channel == 1) {
        return "sub";
    }
    char text[32] = {};
    std::snprintf(text, sizeof(text), "chn%d", channel.channel);
    return std::string(text);
}

bool ParseOsdConfig(const ConfigJson &value, ParsedOsdConfig *config) {
    if (config == nullptr || !value.is_object()) {
        return false;
    }
    const ConfigJson *items = nullptr;
    const ConfigJson *timestamp = nullptr;
    const ConfigJson *device_name = nullptr;
    std::string color_text;
    int32_t x = 0;
    int32_t y = 0;
    if (!json_utils::Load(value, "enabled", &config->enabled) ||
        !json_utils::LoadObject(value, "items", &items) ||
        !json_utils::Load(value, "font_size", &config->font_size,
                          kMinFontSize, kMaxFontSize) ||
        !json_utils::Load(value, "font_color", &color_text) ||
        !ParseHexColor(color_text, &config->font_color) ||
        !json_utils::Load(value, "background", &config->background) ||
        !json_utils::LoadObject(*items, "timestamp", &timestamp) ||
        !json_utils::LoadObject(*items, "device_name", &device_name)) {
        return false;
    }
    if (!json_utils::Load(*timestamp, "enabled",
                          &config->timestamp_enabled) ||
        !json_utils::Load(*timestamp, "format",
                          &config->timestamp_format) ||
        !json_utils::Load(*timestamp, "x", &x) ||
        !json_utils::Load(*timestamp, "y", &y)) {
        return false;
    }
    config->timestamp_position = OsdPoint{x, y};
    if (!json_utils::Load(*device_name, "enabled",
                          &config->device_name_enabled) ||
        !json_utils::Load(*device_name, "text", &config->device_name) ||
        !json_utils::Load(*device_name, "x", &x) ||
        !json_utils::Load(*device_name, "y", &y)) {
        return false;
    }
    config->device_name_position = OsdPoint{x, y};
    return true;
}

}  // namespace

using osd_internal::HostOsdMppAdapter;
using osd_internal::IsValidBitmap;
using osd_internal::IsValidChannel;
using osd_internal::IsValidRegionConfig;
using osd_internal::kMaxRegions;
using osd_internal::MinHandle;

bool ParseOsdItem(const ConfigJson &items, const char *name,
                  OsdRegionConfig *config) {
    if (config == nullptr) {
        return false;
    }
    const ConfigJson *item = nullptr;
    if (!json_utils::LoadObject(items, name, &item)) {
        return false;
    }
    int32_t x = 0;
    int32_t y = 0;
    if (!json_utils::Load(*item, "enabled", &config->visible) ||
        !json_utils::Load(*item, "x", &x) || !json_utils::Load(*item, "y", &y)) {
        return false;
    }
    config->position.x = x;
    config->position.y = y;
    config->size.width = 200;
    config->size.height = 48;
    return IsValidRegionConfig(*config);
}

bool IsValidOsdConfig(const ConfigJson &value) {
    const ConfigJson *items = nullptr;
    bool enabled = false;
    return value.is_object() && json_utils::Load(value, "enabled", &enabled) &&
           json_utils::LoadObject(value, "items", &items);
}

struct OsdService::Impl {
    explicit Impl(const OsdServiceOptions &service_options)
        : options(service_options), mpp(service_options.sdk) {}

    struct RegionRecord {
        OsdRegionId id;
        int32_t mpp_handle = -1;
        std::string name;
        OsdRegionConfig config;
        bool created = false;
        bool attached = false;
        bool has_bitmap = false;
    };

    OsdServiceOptions options;
    ServiceState state = ServiceState::kCreated;
    bool media_bound = false;
    MediaChannels media_channels;
    HostOsdMppAdapter mpp;
    uint32_t next_id = 1;
    std::vector<RegionRecord> regions;
    OsdServiceStats stats;
    mutable std::mutex mutex;
    std::condition_variable refresh_condition;
    std::thread refresh_thread;
    ParsedOsdConfig active_config;
    bool refresh_running = false;
    bool config_attached = false;

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex);
        if (state == ServiceState::kInitialized ||
            state == ServiceState::kStarted || state == ServiceState::kStopped) {
            return true;
        }
        if (options.config_service != nullptr && !config_attached) {
            ConfigAttachment attachment;
            attachment.validate = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex);
                return VerifyConfig(value)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "invalid osd config");
            };
            attachment.apply = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex);
                return ApplyConfig(value)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "apply osd config failed");
            };
            if (!options.config_service->AttachConfig("osd", attachment)) {
                return false;
            }
            config_attached = true;
        }
        state = ServiceState::kInitialized;
        return true;
    }

    void Release() {
        StopRefreshThread();
        {
            std::lock_guard<std::mutex> lock(mutex);
            DestroyAll();
            media_bound = false;
            if (state != ServiceState::kCreated) {
                state = ServiceState::kDeinitialized;
            }
        }
    }

    RegionRecord *Find(OsdRegionId id) {
        for (auto &region : regions) {
            if (region.id.value == id.value) {
                return &region;
            }
        }
        return nullptr;
    }

    const RegionRecord *Find(OsdRegionId id) const {
        for (const auto &region : regions) {
            if (region.id.value == id.value) {
                return &region;
            }
        }
        return nullptr;
    }

    RegionRecord *FindByName(const std::string &name) {
        for (auto &region : regions) {
            if (region.name == name) {
                return &region;
            }
        }
        return nullptr;
    }

    int32_t AllocateHandle(OsdRegionType type) const {
        const int32_t min_handle = MinHandle(type);
        if (min_handle < 0) {
            return -1;
        }
        for (uint32_t offset = 0; offset < kMaxRegions; ++offset) {
            const int32_t candidate = min_handle + static_cast<int32_t>(offset);
            bool used = false;
            for (const auto &region : regions) {
                if (region.mpp_handle == candidate) {
                    used = true;
                    break;
                }
            }
            if (!used) {
                return candidate;
            }
        }
        return -1;
    }

    void DetachAll() {
        for (auto &region : regions) {
            if (region.attached) {
                (void)mpp.Detach(region.mpp_handle, region.config);
            }
            region.attached = false;
        }
    }

    void DestroyAll() {
        DetachAll();
        for (const auto &region : regions) {
            if (region.created) {
                mpp.Destroy(region.mpp_handle);
            }
        }
        regions.clear();
    }

    bool VerifyConfig(const ConfigJson &value) const {
        ParsedOsdConfig parsed;
        return ParseOsdConfig(value, &parsed);
    }

    std::vector<MppChannel> OverlayTargets() const {
        std::vector<MppChannel> targets;
        if (IsValidChannel(media_channels.venc)) {
            targets.push_back(media_channels.venc);
        }
        if (IsValidChannel(media_channels.sub_venc)) {
            bool duplicate = false;
            for (const MppChannel &target : targets) {
                if (target.module == media_channels.sub_venc.module &&
                    target.device == media_channels.sub_venc.device &&
                    target.channel == media_channels.sub_venc.channel) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                targets.push_back(media_channels.sub_venc);
            }
        }
        return targets;
    }

    bool UpsertConfigRegion(const std::string &name,
                            const OsdRegionConfig &config,
                            const TextBitmap &text_bitmap) {
        RegionRecord *region = FindByName(name);
        if (region != nullptr) {
            const bool ok = mpp.SetDisplay(region->mpp_handle, config) &&
                            mpp.UpdateBitmap(region->mpp_handle,
                                             ToOsdBitmap(text_bitmap));
            if (ok) {
                region->config = config;
                region->has_bitmap = true;
                ++stats.bitmap_update_count;
            }
            return ok;
        }

        if (regions.size() >= kMaxRegions) {
            return false;
        }
        const int32_t handle = AllocateHandle(config.type);
        if (handle < 0) {
            return false;
        }
        if (!mpp.Create(handle, config)) {
            return false;
        }
        if (!mpp.Attach(handle, config)) {
            mpp.Destroy(handle);
            return false;
        }
        if (!mpp.UpdateBitmap(handle, ToOsdBitmap(text_bitmap))) {
            (void)mpp.Detach(handle, config);
            mpp.Destroy(handle);
            return false;
        }

        RegionRecord record{};
        record.id.value = next_id++;
        record.mpp_handle = handle;
        record.name = name;
        record.config = config;
        record.created = true;
        record.attached = true;
        record.has_bitmap = true;
        regions.push_back(std::move(record));
        ++stats.bitmap_update_count;
        return true;
    }

    bool UpdateTimestampLocked() {
        if (!active_config.enabled || !active_config.timestamp_enabled) {
            return true;
        }
        const std::string text =
            FormatTimestamp(active_config.timestamp_format);
        if (text.empty()) {
            return false;
        }
        TextBitmap bitmap = RenderTextBitmap(text, active_config.font_size,
                                             active_config.font_color,
                                             active_config.background);
        for (auto &region : regions) {
            if (region.name.find("timestamp:") != 0) {
                continue;
            }
            if (!mpp.UpdateBitmap(region.mpp_handle, ToOsdBitmap(bitmap))) {
                return false;
            }
            region.has_bitmap = true;
            region.config.size = bitmap.size;
            ++stats.bitmap_update_count;
        }
        return true;
    }

    void RefreshLoop() {
        std::unique_lock<std::mutex> lock(mutex);
        while (refresh_running) {
            (void)UpdateTimestampLocked();
            refresh_condition.wait_for(lock, std::chrono::seconds(1), [this] {
                return !refresh_running;
            });
        }
    }

    void StartRefreshThreadLocked() {
        if (refresh_running) {
            return;
        }
        refresh_running = true;
        refresh_thread = std::thread(&Impl::RefreshLoop, this);
    }

    void StopRefreshThread() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            refresh_running = false;
            refresh_condition.notify_all();
        }
        if (refresh_thread.joinable()) {
            refresh_thread.join();
        }
    }

    bool ApplyConfig(const ConfigJson &value) {
        ParsedOsdConfig parsed;
        if (!ParseOsdConfig(value, &parsed)) {
            ++stats.config_apply_failed_count;
            return false;
        }
        active_config = parsed;
        if (state != ServiceState::kStarted || !media_bound) {
            ++stats.config_apply_count;
            return true;
        }
        if (!parsed.enabled) {
            DestroyAll();
            ++stats.config_apply_count;
            stats.region_count = static_cast<uint32_t>(regions.size());
            return true;
        }

        const std::vector<MppChannel> targets = OverlayTargets();
        for (const MppChannel &target : targets) {
            if (parsed.timestamp_enabled) {
                const std::string text =
                    FormatTimestamp(parsed.timestamp_format);
                TextBitmap bitmap = RenderTextBitmap(text, parsed.font_size,
                                                     parsed.font_color,
                                                     parsed.background);
                OsdRegionConfig timestamp;
                timestamp.target = target;
                timestamp.position = parsed.timestamp_position;
                timestamp.size = bitmap.size;
                timestamp.visible = true;
                if (!UpsertConfigRegion("timestamp:" + TargetSuffix(target),
                                        timestamp, bitmap)) {
                    ++stats.config_apply_failed_count;
                    return false;
                }
            }
            if (parsed.device_name_enabled) {
                TextBitmap bitmap =
                    RenderTextBitmap(parsed.device_name, parsed.font_size,
                                     parsed.font_color, parsed.background);
                OsdRegionConfig device_name;
                device_name.target = target;
                device_name.position = parsed.device_name_position;
                device_name.size = bitmap.size;
                device_name.visible = true;
                if (!UpsertConfigRegion("device_name:" + TargetSuffix(target),
                                        device_name, bitmap)) {
                    ++stats.config_apply_failed_count;
                    return false;
                }
            }
        }
        if (parsed.timestamp_enabled) {
            StartRefreshThreadLocked();
        }
        ++stats.config_apply_count;
        stats.region_count = static_cast<uint32_t>(regions.size());
        return true;
    }
};

OsdService::OsdService() : OsdService(OsdServiceOptions{}) {}

OsdService::OsdService(const OsdServiceOptions &options)
    : impl_(new Impl(options)) {}

OsdService::~OsdService() {
    if (impl_ != nullptr) {
        impl_->Release();
        delete impl_;
        impl_ = nullptr;
    }
}

bool OsdService::Start() {
    if (impl_ == nullptr) {
        return false;
    }
    bool need_init = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        need_init = impl_->state == ServiceState::kCreated ||
                    impl_->state == ServiceState::kDeinitialized;
    }
    if (need_init && !impl_->Prepare()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == ServiceState::kStarted) {
        return true;
    }
    if (impl_->state == ServiceState::kStopped) {
        impl_->state = ServiceState::kInitialized;
    }
    if (impl_->state != ServiceState::kInitialized) {
        return false;
    }
    if (!impl_->media_bound) {
        const MediaChannels channels = impl_->options.media_channels;
        if (!IsValidChannel(channels.venc) || !IsValidChannel(channels.vpss)) {
            return false;
        }
        impl_->media_channels = channels;
        impl_->media_bound = true;
    }
    impl_->state = ServiceState::kStarted;
    if (impl_->options.config_service != nullptr) {
        ConfigJson osd_config = impl_->options.config_service->GetValue("osd");
        if (osd_config.is_object()) {
            return impl_->ApplyConfig(osd_config);
        }
    }
    return true;
}

void OsdService::Stop() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->StopRefreshThread();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->DetachAll();
    if (impl_->state == ServiceState::kStarted) {
        impl_->state = ServiceState::kStopped;
    }
}

const char *OsdService::StaticName() { return "osd_service"; }

bool OsdService::BindMedia(const MediaChannels &channels) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == ServiceState::kStarted) {
        return false;
    }
    if (!IsValidChannel(channels.venc) || !IsValidChannel(channels.vpss)) {
        return false;
    }
    impl_->media_channels = channels;
    impl_->media_bound = true;
    return true;
}

OsdRegionId OsdService::CreateRegion(const OsdRegionConfig &config) {
    if (impl_ == nullptr) {
        return OsdRegionId{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != ServiceState::kStarted) {
        return OsdRegionId{};
    }
    if (!IsValidRegionConfig(config)) {
        return OsdRegionId{};
    }
    if (impl_->regions.size() >= kMaxRegions) {
        return OsdRegionId{};
    }

    const int32_t handle = impl_->AllocateHandle(config.type);
    if (handle < 0) {
        return OsdRegionId{};
    }
    if (!impl_->mpp.Create(handle, config)) {
        return OsdRegionId{};
    }

    Impl::RegionRecord record{};
    record.id.value = impl_->next_id++;
    record.mpp_handle = handle;
    record.config = config;
    record.created = true;
    record.attached = false;
    impl_->regions.push_back(std::move(record));
    impl_->stats.region_count = static_cast<uint32_t>(impl_->regions.size());
    return impl_->regions.back().id;
}

bool OsdService::Attach(OsdRegionId id) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::RegionRecord *region = impl_->Find(id);
    if (region == nullptr) {
        return false;
    }
    if (impl_->state != ServiceState::kStarted) {
        return false;
    }
    if (!impl_->mpp.Attach(region->mpp_handle, region->config)) {
        return false;
    }
    region->attached = true;
    return true;
}

bool OsdService::Detach(OsdRegionId id) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::RegionRecord *region = impl_->Find(id);
    if (region == nullptr) {
        return false;
    }
    const bool ok = impl_->mpp.Detach(region->mpp_handle, region->config);
    if (ok) {
        region->attached = false;
    }
    return ok;
}

bool OsdService::SetVisible(OsdRegionId id, bool visible) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::RegionRecord *region = impl_->Find(id);
    if (region == nullptr) {
        return false;
    }
    OsdRegionConfig next_config = region->config;
    next_config.visible = visible;
    const bool ok = impl_->mpp.SetDisplay(region->mpp_handle, next_config);
    if (ok) {
        region->config = next_config;
    }
    return ok;
}

bool OsdService::UpdateBitmap(OsdRegionId id, const OsdBitmap &bitmap) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::RegionRecord *region = impl_->Find(id);
    if (region == nullptr) {
        return false;
    }
    if (!IsValidBitmap(bitmap)) {
        return false;
    }
    OsdRegionConfig next_config = region->config;
    next_config.size = bitmap.dimensions;
    next_config.pixel_format = bitmap.pixel_format;
    if (!IsValidRegionConfig(next_config)) {
        return false;
    }
    const bool ok = impl_->mpp.UpdateBitmap(region->mpp_handle, bitmap);
    if (ok) {
        region->has_bitmap = true;
        region->config = next_config;
        ++impl_->stats.bitmap_update_count;
    }
    return ok;
}

bool OsdService::DestroyRegion(OsdRegionId id) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto iter = impl_->regions.begin(); iter != impl_->regions.end();
         ++iter) {
        if (iter->id.value == id.value) {
            if (iter->attached) {
                (void)impl_->mpp.Detach(iter->mpp_handle, iter->config);
            }
            if (iter->created) {
                impl_->mpp.Destroy(iter->mpp_handle);
            }
            impl_->regions.erase(iter);
            impl_->stats.region_count = static_cast<uint32_t>(impl_->regions.size());
            return true;
        }
    }
    return false;
}

uint32_t OsdService::RegionCount() const {
    if (impl_ == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return static_cast<uint32_t>(impl_->regions.size());
}

OsdServiceStats OsdService::GetStats() const {
    if (impl_ == nullptr) {
        return OsdServiceStats{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    OsdServiceStats stats = impl_->stats;
    stats.region_count = static_cast<uint32_t>(impl_->regions.size());
    return stats;
}

}  // namespace live_stream
