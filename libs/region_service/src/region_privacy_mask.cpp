#include "region_service_internal.h"

#include "infra/log.h"
#include "json_utils.h"

#include <cstdio>

namespace live_stream {
namespace {

bool IsValidVideoSize(const VideoSize &size) {
    return size.width > 0 && size.height > 0;
}

bool IsMaskWithinFrame(const PrivacyMask &mask,
                       const VideoSize &frame_size) {
    if (!mask.enabled) {
        return true;
    }
    if (!IsValidVideoSize(frame_size) || mask.size.width == 0 ||
        mask.size.height == 0 || mask.position.x < 0 ||
        mask.position.y < 0) {
        return false;
    }
    const uint64_t right = static_cast<uint64_t>(mask.position.x) +
                           static_cast<uint64_t>(mask.size.width);
    const uint64_t bottom = static_cast<uint64_t>(mask.position.y) +
                            static_cast<uint64_t>(mask.size.height);
    return right <= frame_size.width && bottom <= frame_size.height;
}

bool ParsePrivacyMask(const ConfigJson &value, const VideoSize &frame_size,
                      PrivacyMask *mask) {
    if (mask == nullptr || !value.is_object()) {
        return false;
    }
    std::string color_text;
    if (!json_utils::ReadField(value, "enabled", &mask->enabled) ||
        !json_utils::ReadField(value, "x", &mask->position.x) ||
        !json_utils::ReadField(value, "y", &mask->position.y) ||
        !json_utils::ReadField(value, "width", &mask->size.width) ||
        !json_utils::ReadField(value, "height", &mask->size.height) ||
        !json_utils::ReadField(value, "color", &color_text) ||
        !ParseHexColor(color_text, &mask->color)) {
        return false;
    }
    return IsMaskWithinFrame(*mask, frame_size);
}

bool ParsePrivacyMaskArray(const ConfigJson &privacy_masks,
                           const char *stream_name,
                           const VideoSize &frame_size,
                           PrivacyMask *masks) {
    if (stream_name == nullptr || masks == nullptr ||
        !privacy_masks.contains(stream_name) ||
        !privacy_masks.at(stream_name).is_array() ||
        privacy_masks.at(stream_name).size() != PrivacyMasks::kSlotCount) {
        return false;
    }
    const ConfigJson &items = privacy_masks.at(stream_name);
    for (uint32_t index = 0; index < PrivacyMasks::kSlotCount; ++index) {
        if (!ParsePrivacyMask(items.at(index), frame_size, &masks[index])) {
            return false;
        }
    }
    return true;
}

std::string PrivacyMaskName(const char *stream_name, uint32_t slot) {
    char text[32] = {};
    std::snprintf(text, sizeof(text), "privacy_mask:%s:%u", stream_name,
                  slot);
    return std::string(text);
}

bool ApplyMaskSet(RegionServiceImpl *service, const char *stream_name,
                  const MppChannel &target, const PrivacyMask *masks) {
    if (service == nullptr || stream_name == nullptr || masks == nullptr) {
        return false;
    }
    if (!IsValidChannel(target)) {
        return true;
    }
    for (uint32_t slot = 0; slot < PrivacyMasks::kSlotCount; ++slot) {
        const std::string name = PrivacyMaskName(stream_name, slot);
        const PrivacyMask &mask = masks[slot];
        if (!mask.enabled) {
            service->DestroyRegionByPrefix(name);
            continue;
        }
        RegionConfig config;
        config.type = RegionType::kCover;
        config.target = target;
        config.position = mask.position;
        config.size = mask.size;
        config.background_color = mask.color;
        config.visible = true;
        if (!service->UpsertDisplayRegion(name, config)) {
            INFRA_LOG_ERROR(
                "region_service",
                "apply privacy mask failed stream=%s slot=%u target=%d:%d:%d "
                "x=%d y=%d width=%u height=%u color=0x%06x",
                stream_name, slot, static_cast<int>(target.module),
                target.device, target.channel, mask.position.x,
                mask.position.y, mask.size.width, mask.size.height,
                mask.color);
            return false;
        }
    }
    return true;
}

}  // namespace

bool ParsePrivacyMasksConfig(const ConfigJson &value,
                             const MediaChannels &channels,
                             PrivacyMasks *masks) {
    if (masks == nullptr || !value.is_object() ||
        !value.contains("privacy_masks") ||
        !value.at("privacy_masks").is_object()) {
        return false;
    }
    const ConfigJson &privacy_masks = value.at("privacy_masks");
    return ParsePrivacyMaskArray(privacy_masks, "main", channels.main_size,
                                 masks->main) &&
           ParsePrivacyMaskArray(privacy_masks, "sub", channels.sub_size,
                                 masks->sub);
}

bool RegionServiceImpl::ApplyPrivacyMasks(const PrivacyMasks &masks) {
    if (!ApplyMaskSet(this, "main", media_channels.vpss, masks.main)) {
        return false;
    }
    if (!ApplyMaskSet(this, "sub", media_channels.sub_vpss, masks.sub)) {
        return false;
    }
    return true;
}

}  // namespace live_stream
