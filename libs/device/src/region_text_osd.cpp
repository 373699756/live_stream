#include "region_overlay.h"

#include "json_utils.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace live_stream {
namespace device_internal {
namespace {

constexpr uint32_t kGlyphWidth = 5;
constexpr uint32_t kGlyphHeight = 7;
constexpr uint32_t kGlyphSpacing = 1;
constexpr uint32_t kTextPaddingX = 4;
constexpr uint32_t kTextPaddingY = 3;
constexpr uint32_t kTextBitmapWidthAlignment = 16;
constexpr uint32_t kMaxRenderedFontSize = 32;

bool IsValidVideoSize(const VideoSize &size) {
    return size.width > 0 && size.height > 0;
}

bool SameChannel(const MppChannel &left, const MppChannel &right) {
    return left.module == right.module && left.device == right.device &&
           left.channel == right.channel;
}

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
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
    const uint32_t rendered_font_size =
        std::min(font_size, kMaxRenderedFontSize);
    const uint32_t scale =
        std::max<uint32_t>(1, rendered_font_size / kGlyphHeight);
    const uint32_t glyph_width = kGlyphWidth * scale;
    const uint32_t glyph_height = kGlyphHeight * scale;
    const uint32_t spacing = kGlyphSpacing * scale;
    const uint32_t text_width =
        text.empty() ? glyph_width
                     : static_cast<uint32_t>(text.size()) *
                               (glyph_width + spacing) -
                           spacing;
    bitmap.size.width =
        AlignUp(text_width + kTextPaddingX * 2, kTextBitmapWidthAlignment);
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

uint32_t RenderedFontSizeForTarget(uint32_t font_size,
                                   const MediaChannels &media_channels,
                                   const MppChannel &target) {
    const uint32_t capped_font_size =
        std::min(font_size, kMaxRenderedFontSize);
    if (!SameChannel(target, media_channels.sub_venc) ||
        !IsValidVideoSize(media_channels.main_size) ||
        !IsValidVideoSize(media_channels.sub_size)) {
        return capped_font_size;
    }
    const uint32_t scaled_by_width =
        static_cast<uint32_t>(
            (static_cast<uint64_t>(capped_font_size) *
             media_channels.sub_size.width) /
            media_channels.main_size.width);
    const uint32_t scaled_by_height =
        static_cast<uint32_t>(
            (static_cast<uint64_t>(capped_font_size) *
             media_channels.sub_size.height) /
            media_channels.main_size.height);
    const uint32_t scaled_font_size =
        std::min(scaled_by_width, scaled_by_height);
    return std::max<uint32_t>(1, std::min(capped_font_size,
                                          scaled_font_size));
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

}  // namespace

bool ParseTextOverlayConfig(const ConfigJson &value,
                            ParsedOverlayConfig *config) {
    if (config == nullptr || !value.is_object()) {
        return false;
    }
    std::string color_text;
    int32_t x = 0;
    int32_t y = 0;
    if (!json_utils::ReadField(value, "enabled", &config->enabled) ||
        !value.contains("items") || !value.at("items").is_object() ||
        !json_utils::ReadField(value, "font_size", &config->font_size,
                               kMinFontSize, kMaxFontSize) ||
        !json_utils::ReadField(value, "font_color", &color_text) ||
        !ParseHexColor(color_text, &config->font_color) ||
        !json_utils::ReadField(value, "background", &config->background)) {
        return false;
    }
    const ConfigJson &items = value.at("items");
    if (!items.contains("timestamp") || !items.at("timestamp").is_object() ||
        !items.contains("device_name") ||
        !items.at("device_name").is_object()) {
        return false;
    }
    const ConfigJson &timestamp = items.at("timestamp");
    const ConfigJson &device_name = items.at("device_name");
    if (!json_utils::ReadField(timestamp, "enabled",
                               &config->timestamp_enabled) ||
        !json_utils::ReadField(timestamp, "format",
                               &config->timestamp_format) ||
        !json_utils::ReadField(timestamp, "x", &x) ||
        !json_utils::ReadField(timestamp, "y", &y)) {
        return false;
    }
    config->timestamp_position = RegionPoint{x, y};
    if (!json_utils::ReadField(device_name, "enabled",
                               &config->device_name_enabled) ||
        !json_utils::ReadField(device_name, "text", &config->device_name) ||
        !json_utils::ReadField(device_name, "x", &x) ||
        !json_utils::ReadField(device_name, "y", &y)) {
        return false;
    }
    config->device_name_position = RegionPoint{x, y};
    return true;
}

bool RegionOverlay::ApplyTextOverlay(
    const ParsedOverlayConfig &config) {
    if (!config.enabled) {
        DestroyRegionByPrefix("timestamp:");
        DestroyRegionByPrefix("device_name:");
        return true;
    }

    const std::vector<MppChannel> targets = OverlayTargets();
    for (const MppChannel &target : targets) {
        if (config.timestamp_enabled) {
            const std::string text = FormatTimestamp(config.timestamp_format);
            const uint32_t font_size =
                RenderedFontSizeForTarget(config.font_size, media_channels,
                                          target);
            TextBitmap bitmap = RenderTextBitmap(text, font_size,
                                                 config.font_color,
                                                 config.background);
            RegionConfig timestamp;
            timestamp.target = target;
            timestamp.position = config.timestamp_position;
            timestamp.size = bitmap.size;
            timestamp.visible = true;
            if (!UpsertBitmapRegion(
                    "timestamp:" + RegionTargetSuffix(target), timestamp,
                    bitmap)) {
                return false;
            }
        } else {
            DestroyRegionByPrefix("timestamp:");
        }

        if (config.device_name_enabled) {
            const uint32_t font_size =
                RenderedFontSizeForTarget(config.font_size, media_channels,
                                          target);
            TextBitmap bitmap = RenderTextBitmap(config.device_name,
                                                 font_size, config.font_color,
                                                 config.background);
            RegionConfig device_name;
            device_name.target = target;
            device_name.position = config.device_name_position;
            device_name.size = bitmap.size;
            device_name.visible = true;
            if (!UpsertBitmapRegion(
                    "device_name:" + RegionTargetSuffix(target), device_name,
                    bitmap)) {
                return false;
            }
        } else {
            DestroyRegionByPrefix("device_name:");
        }
    }
    if (config.timestamp_enabled) {
        StartRefreshThreadLocked();
    }
    return true;
}

bool RegionOverlay::UpdateTimestampLocked() {
    if (!active_config.enabled || !active_config.timestamp_enabled) {
        return true;
    }
    const std::string text = FormatTimestamp(active_config.timestamp_format);
    if (text.empty()) {
        return false;
    }
    bool need_rebuild = false;
    for (auto &region : regions) {
        if (region.name.find("timestamp:") != 0) {
            continue;
        }
        const uint32_t font_size =
            RenderedFontSizeForTarget(active_config.font_size, media_channels,
                                      region.config.target);
        TextBitmap bitmap = RenderTextBitmap(text, font_size,
                                             active_config.font_color,
                                             active_config.background);
        const RegionBitmap region_bitmap = BuildRegionBitmap(bitmap);
        if (!sdk->SetRegionBitmap(region.mpp_handle,
                                  BuildSdkBitmap(region_bitmap))) {
            need_rebuild = true;
            break;
        }
        region.has_bitmap = true;
        region.config.size = bitmap.size;
        ++stats.bitmap_update_count;
    }
    if (need_rebuild) {
        return ApplyTextOverlay(active_config);
    }
    return true;
}

void RegionOverlay::StartRefreshThreadLocked() {
    if (refresh_running) {
        return;
    }
    refresh_running = true;
    refresh_thread = std::thread([this]() {
        std::unique_lock<std::mutex> lock(mutex);
        while (refresh_running) {
            (void)UpdateTimestampLocked();
            refresh_condition.wait_for(lock, std::chrono::seconds(1), [this] {
                return !refresh_running;
            });
        }
    });
}

void RegionOverlay::StopRefreshThread() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        refresh_running = false;
        refresh_condition.notify_all();
    }
    if (refresh_thread.joinable()) {
        refresh_thread.join();
    }
}

}  // namespace device_internal
}  // namespace live_stream
