#include "region_service_internal.h"

#include <cstdio>
#include <utility>

namespace live_stream {
namespace {

constexpr int32_t kOverlayMinHandle = 0;
constexpr int32_t kOverlayExMinHandle = 20;
constexpr int32_t kCoverMinHandle = 40;
constexpr int32_t kCoverExMinHandle = 60;
constexpr int32_t kMosaicMinHandle = 80;

bool IsValidSize(const RegionSize &size) {
    return size.width > 0 && size.height > 0;
}

bool IsAligned(uint32_t value, uint32_t alignment) {
    return alignment == 0 || value % alignment == 0;
}

bool IsAlignedPoint(const RegionPoint &point, int32_t alignment_x,
                    int32_t alignment_y) {
    return point.x >= 0 && point.y >= 0 &&
           point.x % alignment_x == 0 && point.y % alignment_y == 0;
}

bool IsSupportedTarget(RegionType type, MppModule module) {
    switch (type) {
        case RegionType::kOverlay:
            return module == MppModule::kVenc;
        case RegionType::kOverlayEx:
            return module == MppModule::kVi || module == MppModule::kVpss ||
                   module == MppModule::kVo;
        case RegionType::kCover:
            return module == MppModule::kVpss;
        case RegionType::kCoverEx:
            return module == MppModule::kVi || module == MppModule::kVpss ||
                   module == MppModule::kVo;
        case RegionType::kMosaic:
            return module == MppModule::kVpss;
    }
    return false;
}

hisisdk::RegionType BuildSdkRegionType(RegionType type) {
    switch (type) {
        case RegionType::kOverlay:
            return hisisdk::RegionType::kOverlay;
        case RegionType::kOverlayEx:
            return hisisdk::RegionType::kOverlayEx;
        case RegionType::kCover:
            return hisisdk::RegionType::kCover;
        case RegionType::kCoverEx:
            return hisisdk::RegionType::kCoverEx;
        case RegionType::kMosaic:
            return hisisdk::RegionType::kMosaic;
    }
    return hisisdk::RegionType::kOverlay;
}

hisisdk::PixelFormat BuildSdkPixelFormat(RegionPixelFormat format) {
    switch (format) {
        case RegionPixelFormat::kArgb1555:
            return hisisdk::PixelFormat::kArgb1555;
        case RegionPixelFormat::kArgb4444:
            return hisisdk::PixelFormat::kArgb4444;
        case RegionPixelFormat::kArgb8888:
            return hisisdk::PixelFormat::kArgb8888;
        case RegionPixelFormat::kArgb2Bpp:
            return hisisdk::PixelFormat::kArgb2Bpp;
    }
    return hisisdk::PixelFormat::kArgb1555;
}

}  // namespace

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

bool IsValidChannel(const MppChannel &channel) {
    return channel.device >= 0 && channel.channel >= 0;
}

bool IsValidRegionConfig(const RegionConfig &config) {
    if (!IsValidSize(config.size) || !IsValidChannel(config.target) ||
        !IsSupportedTarget(config.type, config.target.module)) {
        return false;
    }
    switch (config.type) {
        case RegionType::kOverlay:
        case RegionType::kOverlayEx:
            return IsAligned(config.size.width, 2) &&
                   IsAligned(config.size.height, 2) &&
                   IsAlignedPoint(config.position, 2, 2);
        case RegionType::kMosaic:
            return config.size.width >= 32 && config.size.height >= 32 &&
                   IsAligned(config.size.width, 4) &&
                   IsAligned(config.size.height, 4) &&
                   IsAlignedPoint(config.position, 4, 2);
        case RegionType::kCover:
        case RegionType::kCoverEx:
            return true;
    }
    return false;
}

bool IsValidBitmap(const RegionBitmap &bitmap) {
    const uint64_t min_size =
        static_cast<uint64_t>(bitmap.stride) * bitmap.dimensions.height;
    return bitmap.data != nullptr && bitmap.size > 0 && bitmap.stride > 0 &&
           IsValidSize(bitmap.dimensions) && min_size <= bitmap.size;
}

int32_t MinHandle(RegionType type) {
    switch (type) {
        case RegionType::kOverlay:
            return kOverlayMinHandle;
        case RegionType::kOverlayEx:
            return kOverlayExMinHandle;
        case RegionType::kCover:
            return kCoverMinHandle;
        case RegionType::kCoverEx:
            return kCoverExMinHandle;
        case RegionType::kMosaic:
            return kMosaicMinHandle;
    }
    return -1;
}

RegionBitmap BuildRegionBitmap(const TextBitmap &text_bitmap) {
    RegionBitmap bitmap;
    bitmap.data = text_bitmap.pixels.data();
    bitmap.size = static_cast<uint32_t>(text_bitmap.pixels.size());
    bitmap.stride = text_bitmap.stride;
    bitmap.dimensions = text_bitmap.size;
    bitmap.pixel_format = RegionPixelFormat::kArgb1555;
    return bitmap;
}

hisisdk::RegionConfig BuildSdkRegionConfig(const RegionConfig &config) {
    hisisdk::RegionConfig sdk_config;
    sdk_config.type = BuildSdkRegionType(config.type);
    sdk_config.pixel_format = BuildSdkPixelFormat(config.pixel_format);
    sdk_config.size = hisisdk::Size{config.size.width, config.size.height};
    sdk_config.position =
        hisisdk::Point{config.position.x, config.position.y};
    sdk_config.background_color = config.background_color;
    sdk_config.foreground_alpha = config.foreground_alpha;
    sdk_config.background_alpha = config.background_alpha;
    sdk_config.visible = config.visible;
    sdk_config.target = config.target;
    return sdk_config;
}

hisisdk::Bitmap BuildSdkBitmap(const RegionBitmap &bitmap) {
    hisisdk::Bitmap sdk_bitmap;
    sdk_bitmap.data = bitmap.data;
    sdk_bitmap.size = bitmap.size;
    sdk_bitmap.stride = bitmap.stride;
    sdk_bitmap.dimensions =
        hisisdk::Size{bitmap.dimensions.width, bitmap.dimensions.height};
    sdk_bitmap.pixel_format = BuildSdkPixelFormat(bitmap.pixel_format);
    return sdk_bitmap;
}

std::string RegionTargetSuffix(const MppChannel &channel) {
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

RegionServiceImpl::RegionServiceImpl(
    const RegionServiceOptions &service_options)
    : options(service_options),
      sdk(service_options.sdk != nullptr ? service_options.sdk
                                         : &hisisdk::DefaultSdk()) {}

bool RegionServiceImpl::Prepare() {
    std::lock_guard<std::mutex> lock(mutex);
    if (state == RegionServiceState::kInitialized ||
        state == RegionServiceState::kStarted ||
        state == RegionServiceState::kStopped) {
        return true;
    }
    if (options.config_service != nullptr && !config_attached) {
        ConfigAttachment attachment;
        attachment.validate = [this](const ConfigJson &value) {
            std::lock_guard<std::mutex> guard(mutex);
            return VerifyConfig(value)
                       ? ConfigResult::Success()
                       : ConfigResult::Failure("", "invalid overlay config");
        };
        attachment.apply = [this](const ConfigJson &value) {
            std::lock_guard<std::mutex> guard(mutex);
            return ApplyConfig(value)
                       ? ConfigResult::Success()
                       : ConfigResult::Failure("", "apply overlay config failed");
        };
        if (!options.config_service->AttachConfig("overlay", attachment)) {
            return false;
        }
        config_attached = true;
    }
    state = RegionServiceState::kInitialized;
    return true;
}

void RegionServiceImpl::Release() {
    StopRefreshThread();
    {
        std::lock_guard<std::mutex> lock(mutex);
        DestroyAll();
        media_bound = false;
        if (state != RegionServiceState::kCreated) {
            state = RegionServiceState::kDeinitialized;
        }
    }
}

RegionServiceImpl::RegionRecord *RegionServiceImpl::Find(RegionId id) {
    for (auto &region : regions) {
        if (region.id.value == id.value) {
            return &region;
        }
    }
    return nullptr;
}

const RegionServiceImpl::RegionRecord *RegionServiceImpl::Find(
    RegionId id) const {
    for (const auto &region : regions) {
        if (region.id.value == id.value) {
            return &region;
        }
    }
    return nullptr;
}

RegionServiceImpl::RegionRecord *RegionServiceImpl::FindByName(
    const std::string &name) {
    for (auto &region : regions) {
        if (region.name == name) {
            return &region;
        }
    }
    return nullptr;
}

int32_t RegionServiceImpl::AllocateHandle(RegionType type) const {
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

void RegionServiceImpl::DetachAll() {
    for (auto &region : regions) {
        if (region.attached) {
            (void)sdk->DetachRegion(region.mpp_handle,
                                    BuildSdkRegionConfig(region.config));
        }
        region.attached = false;
    }
}

void RegionServiceImpl::DestroyAll() {
    DetachAll();
    for (const auto &region : regions) {
        if (region.created) {
            sdk->DestroyRegion(region.mpp_handle);
        }
    }
    regions.clear();
}

void RegionServiceImpl::DestroyRegionByPrefix(const std::string &prefix) {
    for (auto iter = regions.begin(); iter != regions.end();) {
        if (iter->name.find(prefix) != 0) {
            ++iter;
            continue;
        }
        if (iter->attached) {
            (void)sdk->DetachRegion(iter->mpp_handle,
                                    BuildSdkRegionConfig(iter->config));
        }
        if (iter->created) {
            sdk->DestroyRegion(iter->mpp_handle);
        }
        iter = regions.erase(iter);
    }
}

MediaChannels RegionServiceImpl::ActiveChannels() const {
    return media_bound ? media_channels : options.media_channels;
}

std::vector<MppChannel> RegionServiceImpl::OverlayTargets() const {
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

bool RegionServiceImpl::UpsertBitmapRegion(const std::string &name,
                                           const RegionConfig &config,
                                           const TextBitmap &text_bitmap) {
    RegionRecord *region = FindByName(name);
    const RegionBitmap bitmap = BuildRegionBitmap(text_bitmap);
    if (region != nullptr) {
        const bool ok =
            sdk->SetRegionDisplay(region->mpp_handle,
                                  BuildSdkRegionConfig(config)) &&
            sdk->SetRegionBitmap(region->mpp_handle, BuildSdkBitmap(bitmap));
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
    if (!sdk->CreateRegion(handle, BuildSdkRegionConfig(config))) {
        return false;
    }
    if (!sdk->AttachRegion(handle, BuildSdkRegionConfig(config))) {
        sdk->DestroyRegion(handle);
        return false;
    }
    if (!sdk->SetRegionBitmap(handle, BuildSdkBitmap(bitmap))) {
        (void)sdk->DetachRegion(handle, BuildSdkRegionConfig(config));
        sdk->DestroyRegion(handle);
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

bool RegionServiceImpl::UpsertDisplayRegion(const std::string &name,
                                            const RegionConfig &config) {
    RegionRecord *region = FindByName(name);
    if (region != nullptr) {
        const bool ok = sdk->SetRegionDisplay(
            region->mpp_handle, BuildSdkRegionConfig(config));
        if (ok) {
            region->config = config;
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
    if (!sdk->CreateRegion(handle, BuildSdkRegionConfig(config))) {
        return false;
    }
    if (!sdk->AttachRegion(handle, BuildSdkRegionConfig(config))) {
        sdk->DestroyRegion(handle);
        return false;
    }

    RegionRecord record{};
    record.id.value = next_id++;
    record.mpp_handle = handle;
    record.name = name;
    record.config = config;
    record.created = true;
    record.attached = true;
    record.has_bitmap = false;
    regions.push_back(std::move(record));
    return true;
}

bool RegionServiceImpl::VerifyConfig(const ConfigJson &value) const {
    ParsedOverlayConfig parsed;
    return ParseTextOverlayConfig(value, &parsed) &&
           ParsePrivacyMasksConfig(value, ActiveChannels(),
                                   &parsed.privacy_masks);
}

bool RegionServiceImpl::ApplyConfig(const ConfigJson &value) {
    ParsedOverlayConfig parsed;
    if (!ParseTextOverlayConfig(value, &parsed) ||
        !ParsePrivacyMasksConfig(value, ActiveChannels(),
                                 &parsed.privacy_masks)) {
        ++stats.config_apply_failed_count;
        return false;
    }
    active_config = parsed;
    if (state != RegionServiceState::kStarted || !media_bound) {
        ++stats.config_apply_count;
        return true;
    }
    if (!ApplyTextOverlay(parsed) ||
        !ApplyPrivacyMasks(parsed.privacy_masks)) {
        ++stats.config_apply_failed_count;
        return false;
    }
    ++stats.config_apply_count;
    stats.region_count = static_cast<uint32_t>(regions.size());
    return true;
}

RegionService::RegionService() : RegionService(RegionServiceOptions{}) {}

RegionService::RegionService(const RegionServiceOptions &options)
    : impl_(new RegionServiceImpl(options)) {}

RegionService::~RegionService() {
    if (impl_ != nullptr) {
        impl_->Release();
        delete impl_;
        impl_ = nullptr;
    }
}

bool RegionService::Start() {
    if (impl_ == nullptr) {
        return false;
    }
    bool need_init = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        need_init = impl_->state == RegionServiceState::kCreated ||
                    impl_->state == RegionServiceState::kDeinitialized;
    }
    if (need_init && !impl_->Prepare()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == RegionServiceState::kStarted) {
        return true;
    }
    if (impl_->state == RegionServiceState::kStopped) {
        impl_->state = RegionServiceState::kInitialized;
    }
    if (impl_->state != RegionServiceState::kInitialized) {
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
    impl_->state = RegionServiceState::kStarted;
    if (impl_->options.config_service != nullptr) {
        ConfigJson overlay_config =
            impl_->options.config_service->GetValue("overlay");
        if (overlay_config.is_object()) {
            return impl_->ApplyConfig(overlay_config);
        }
    }
    return true;
}

void RegionService::Stop() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->StopRefreshThread();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->DetachAll();
    if (impl_->state == RegionServiceState::kStarted) {
        impl_->state = RegionServiceState::kStopped;
    }
}

const char *RegionService::StaticName() { return "region_service"; }

bool RegionService::BindMedia(const MediaChannels &channels) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == RegionServiceState::kStarted) {
        return false;
    }
    if (!IsValidChannel(channels.venc) || !IsValidChannel(channels.vpss)) {
        return false;
    }
    impl_->media_channels = channels;
    impl_->media_bound = true;
    return true;
}

RegionId RegionService::CreateRegion(const RegionConfig &config) {
    if (impl_ == nullptr) {
        return RegionId{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != RegionServiceState::kStarted ||
        !IsValidRegionConfig(config) || impl_->regions.size() >= kMaxRegions) {
        return RegionId{};
    }

    const int32_t handle = impl_->AllocateHandle(config.type);
    if (handle < 0 ||
        !impl_->sdk->CreateRegion(handle, BuildSdkRegionConfig(config))) {
        return RegionId{};
    }

    RegionServiceImpl::RegionRecord record{};
    record.id.value = impl_->next_id++;
    record.mpp_handle = handle;
    record.config = config;
    record.created = true;
    record.attached = false;
    impl_->regions.push_back(std::move(record));
    impl_->stats.region_count =
        static_cast<uint32_t>(impl_->regions.size());
    return impl_->regions.back().id;
}

bool RegionService::Attach(RegionId id) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RegionServiceImpl::RegionRecord *region = impl_->Find(id);
    if (region == nullptr || impl_->state != RegionServiceState::kStarted) {
        return false;
    }
    if (!impl_->sdk->AttachRegion(region->mpp_handle,
                                  BuildSdkRegionConfig(region->config))) {
        return false;
    }
    region->attached = true;
    return true;
}

bool RegionService::Detach(RegionId id) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RegionServiceImpl::RegionRecord *region = impl_->Find(id);
    if (region == nullptr) {
        return false;
    }
    const bool ok = impl_->sdk->DetachRegion(
        region->mpp_handle, BuildSdkRegionConfig(region->config));
    if (ok) {
        region->attached = false;
    }
    return ok;
}

bool RegionService::SetVisible(RegionId id, bool visible) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RegionServiceImpl::RegionRecord *region = impl_->Find(id);
    if (region == nullptr) {
        return false;
    }
    RegionConfig next_config = region->config;
    next_config.visible = visible;
    const bool ok = impl_->sdk->SetRegionDisplay(
        region->mpp_handle, BuildSdkRegionConfig(next_config));
    if (ok) {
        region->config = next_config;
    }
    return ok;
}

bool RegionService::UpdateBitmap(RegionId id, const RegionBitmap &bitmap) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RegionServiceImpl::RegionRecord *region = impl_->Find(id);
    if (region == nullptr || !IsValidBitmap(bitmap)) {
        return false;
    }
    RegionConfig next_config = region->config;
    next_config.size = bitmap.dimensions;
    next_config.pixel_format = bitmap.pixel_format;
    if (!IsValidRegionConfig(next_config)) {
        return false;
    }
    const bool ok = impl_->sdk->SetRegionBitmap(region->mpp_handle,
                                               BuildSdkBitmap(bitmap));
    if (ok) {
        region->has_bitmap = true;
        region->config = next_config;
        ++impl_->stats.bitmap_update_count;
    }
    return ok;
}

bool RegionService::DestroyRegion(RegionId id) {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto iter = impl_->regions.begin(); iter != impl_->regions.end();
         ++iter) {
        if (iter->id.value == id.value) {
            if (iter->attached) {
                (void)impl_->sdk->DetachRegion(
                    iter->mpp_handle, BuildSdkRegionConfig(iter->config));
            }
            if (iter->created) {
                impl_->sdk->DestroyRegion(iter->mpp_handle);
            }
            impl_->regions.erase(iter);
            impl_->stats.region_count =
                static_cast<uint32_t>(impl_->regions.size());
            return true;
        }
    }
    return false;
}

uint32_t RegionService::RegionCount() const {
    if (impl_ == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return static_cast<uint32_t>(impl_->regions.size());
}

RegionServiceStats RegionService::GetStats() const {
    if (impl_ == nullptr) {
        return RegionServiceStats{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    RegionServiceStats stats = impl_->stats;
    stats.region_count = static_cast<uint32_t>(impl_->regions.size());
    return stats;
}

}  // namespace live_stream
