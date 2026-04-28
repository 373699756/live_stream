#include "osd_service.h"

#include "config_service.h"
#include "osd_region.h"

#include <mutex>
#include <string>
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

}  // namespace

using osd_internal::HostOsdMppAdapter;
using osd_internal::IsValidBitmap;
using osd_internal::IsValidChannel;
using osd_internal::IsValidRegionConfig;
using osd_internal::kMaxRegions;
using osd_internal::MinHandle;

bool ParseOsdItem(const ConfigJson& items, const char* name,
                  OsdRegionConfig* config) {
    if (config == nullptr || !items.contains(name) || !items[name].is_object()) {
        return false;
    }
    const ConfigJson& item = items[name];
    config->visible = item.value("enabled", true);
    config->position.x = item.value("x", 0);
    config->position.y = item.value("y", 0);
    config->size.width = 200;
    config->size.height = 48;
    return IsValidRegionConfig(*config);
}

bool IsValidOsdConfig(const ConfigJson& value) {
    return value.is_object() && value.contains("enabled") &&
           value["enabled"].is_boolean() && value.contains("items") &&
           value["items"].is_object();
}

struct OsdService::Impl {
    explicit Impl(const OsdServiceOptions& service_options)
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
    MediaChannels media_channels{};
    HostOsdMppAdapter mpp;
    uint32_t next_id = 1;
    std::vector<RegionRecord> regions;
    OsdServiceStats stats;
    mutable std::mutex mutex;
    bool config_callbacks_registered = false;

    RegionRecord* Find(OsdRegionId id) {
        for (auto& region : regions) {
            if (region.id.value == id.value) {
                return &region;
            }
        }
        return nullptr;
    }

    const RegionRecord* Find(OsdRegionId id) const {
        for (const auto& region : regions) {
            if (region.id.value == id.value) {
                return &region;
            }
        }
        return nullptr;
    }

    RegionRecord* FindByName(const std::string& name) {
        for (auto& region : regions) {
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
            for (const auto& region : regions) {
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
        for (auto& region : regions) {
            if (region.attached) {
                (void)mpp.Detach(region.mpp_handle, region.config);
            }
            region.attached = false;
        }
    }

    void DestroyAll() {
        DetachAll();
        for (const auto& region : regions) {
            if (region.created) {
                mpp.Destroy(region.mpp_handle);
            }
        }
        regions.clear();
    }

    infra::Status VerifyConfig(const ConfigJson& value) const {
        if (!IsValidOsdConfig(value)) {
            return infra::Status::kInvalidParam;
        }
        OsdRegionConfig base;
        base.target = media_bound ? media_channels.venc
                                  : MppChannel{MppModule::kVenc, 0, 0};
        const ConfigJson& items = value["items"];
        OsdRegionConfig timestamp = base;
        OsdRegionConfig device_name = base;
        if (items.contains("timestamp") &&
            !ParseOsdItem(items, "timestamp", &timestamp)) {
            return infra::Status::kInvalidParam;
        }
        if (items.contains("device_name") &&
            !ParseOsdItem(items, "device_name", &device_name)) {
            return infra::Status::kInvalidParam;
        }
        return infra::Status::kOk;
    }

    infra::Status UpsertConfigRegion(const std::string& name,
                                     const OsdRegionConfig& config) {
        RegionRecord* region = FindByName(name);
        if (region != nullptr) {
            const infra::Status status =
                mpp.SetDisplay(region->mpp_handle, config);
            if (status == infra::Status::kOk) {
                region->config = config;
            }
            return status;
        }

        if (regions.size() >= kMaxRegions) {
            return infra::Status::kNoMemory;
        }
        const int32_t handle = AllocateHandle(config.type);
        if (handle < 0) {
            return infra::Status::kNoMemory;
        }
        infra::Status status = mpp.Create(handle, config);
        if (status != infra::Status::kOk) {
            return status;
        }
        status = mpp.Attach(handle, config);
        if (status != infra::Status::kOk) {
            mpp.Destroy(handle);
            return status;
        }

        RegionRecord record{};
        record.id.value = next_id++;
        record.mpp_handle = handle;
        record.name = name;
        record.config = config;
        record.created = true;
        record.attached = true;
        regions.push_back(std::move(record));
        return infra::Status::kOk;
    }

    infra::Status ApplyConfig(const ConfigJson& value) {
        const infra::Status verify_status = VerifyConfig(value);
        if (verify_status != infra::Status::kOk) {
            ++stats.config_apply_failed_count;
            return verify_status;
        }
        if (state != ServiceState::kStarted || !media_bound) {
            ++stats.config_apply_count;
            return infra::Status::kOk;
        }
        if (!value.value("enabled", true)) {
            DestroyAll();
            ++stats.config_apply_count;
            stats.region_count = static_cast<uint32_t>(regions.size());
            return infra::Status::kOk;
        }

        const ConfigJson& items = value["items"];
        OsdRegionConfig base;
        base.target = media_channels.venc;
        OsdRegionConfig timestamp = base;
        if (ParseOsdItem(items, "timestamp", &timestamp)) {
            const infra::Status status =
                UpsertConfigRegion("timestamp", timestamp);
            if (status != infra::Status::kOk) {
                ++stats.config_apply_failed_count;
                return status;
            }
        }
        OsdRegionConfig device_name = base;
        if (ParseOsdItem(items, "device_name", &device_name)) {
            const infra::Status status =
                UpsertConfigRegion("device_name", device_name);
            if (status != infra::Status::kOk) {
                ++stats.config_apply_failed_count;
                return status;
            }
        }
        ++stats.config_apply_count;
        stats.region_count = static_cast<uint32_t>(regions.size());
        return infra::Status::kOk;
    }
};

OsdService::OsdService() : OsdService(OsdServiceOptions{}) {}

OsdService::OsdService(const OsdServiceOptions& options)
    : impl_(new Impl(options)) {}

OsdService::~OsdService() {
    Deinit();
    delete impl_;
    impl_ = nullptr;
}

infra::Status OsdService::Init() {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == ServiceState::kInitialized ||
        impl_->state == ServiceState::kStarted ||
        impl_->state == ServiceState::kStopped) {
        return infra::Status::kOk;
    }
    if (impl_->options.config_service != nullptr &&
        !impl_->config_callbacks_registered) {
        infra::Status status = impl_->options.config_service->RegisterVerify(
            "osd", [this](const ConfigJson& value) {
                std::lock_guard<std::mutex> guard(impl_->mutex);
                return impl_->VerifyConfig(value);
            });
        if (status != infra::Status::kOk) {
            return status;
        }
        status = impl_->options.config_service->RegisterApply(
            "osd", [this](const ConfigJson& value) {
                std::lock_guard<std::mutex> guard(impl_->mutex);
                return impl_->ApplyConfig(value);
            });
        if (status != infra::Status::kOk) {
            return status;
        }
        impl_->config_callbacks_registered = true;
    }
    impl_->state = ServiceState::kInitialized;
    return infra::Status::kOk;
}

infra::Status OsdService::Start() {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == ServiceState::kStarted) {
        return infra::Status::kOk;
    }
    if (impl_->state == ServiceState::kStopped) {
        impl_->state = ServiceState::kInitialized;
    }
    if (impl_->state != ServiceState::kInitialized) {
        return infra::Status::kBusy;
    }
    if (!impl_->media_bound) {
        return infra::Status::kBusy;
    }
    impl_->state = ServiceState::kStarted;
    if (impl_->options.config_service != nullptr) {
        ConfigJson osd_config;
        if (impl_->options.config_service->GetValue("osd", &osd_config) ==
            infra::Status::kOk) {
            return impl_->ApplyConfig(osd_config);
        }
    }
    return infra::Status::kOk;
}

void OsdService::Stop() {
    if (impl_ == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->DetachAll();
    if (impl_->state == ServiceState::kStarted) {
        impl_->state = ServiceState::kStopped;
    }
}

void OsdService::Deinit() {
    if (impl_ == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->DestroyAll();
    impl_->media_bound = false;
    if (impl_->state != ServiceState::kCreated) {
        impl_->state = ServiceState::kDeinitialized;
    }
}

const char* OsdService::Name() const {
    return StaticName();
}

const char* OsdService::StaticName() {
    return "osd_service";
}

infra::Status OsdService::BindMedia(const MediaChannels& channels) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == ServiceState::kStarted) {
        return infra::Status::kBusy;
    }
    if (!IsValidChannel(channels.venc) || !IsValidChannel(channels.vpss)) {
        return infra::Status::kInvalidParam;
    }
    impl_->media_channels = channels;
    impl_->media_bound = true;
    return infra::Status::kOk;
}

infra::Result<OsdRegionId> OsdService::CreateRegion(const OsdRegionConfig& config) {
    if (impl_ == nullptr) {
        return infra::Result<OsdRegionId>::Fail(infra::Status::kInternalError);
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != ServiceState::kStarted) {
        return infra::Result<OsdRegionId>::Fail(infra::Status::kBusy);
    }
    if (!IsValidRegionConfig(config)) {
        return infra::Result<OsdRegionId>::Fail(infra::Status::kInvalidParam);
    }
    if (impl_->regions.size() >= kMaxRegions) {
        return infra::Result<OsdRegionId>::Fail(infra::Status::kNoMemory);
    }

    const int32_t handle = impl_->AllocateHandle(config.type);
    if (handle < 0) {
        return infra::Result<OsdRegionId>::Fail(infra::Status::kNoMemory);
    }
    const infra::Status create_status = impl_->mpp.Create(handle, config);
    if (create_status != infra::Status::kOk) {
        return infra::Result<OsdRegionId>::Fail(create_status);
    }

    Impl::RegionRecord record{};
    record.id.value = impl_->next_id++;
    record.mpp_handle = handle;
    record.config = config;
    record.created = true;
    record.attached = false;
    impl_->regions.push_back(std::move(record));
    impl_->stats.region_count = static_cast<uint32_t>(impl_->regions.size());
    return infra::Result<OsdRegionId>::Ok(impl_->regions.back().id);
}

infra::Status OsdService::Attach(OsdRegionId id) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::RegionRecord* region = impl_->Find(id);
    if (region == nullptr) {
        return infra::Status::kNotFound;
    }
    if (impl_->state != ServiceState::kStarted) {
        return infra::Status::kBusy;
    }
    const infra::Status status =
        impl_->mpp.Attach(region->mpp_handle, region->config);
    if (status != infra::Status::kOk) {
        return status;
    }
    region->attached = true;
    return infra::Status::kOk;
}

infra::Status OsdService::Detach(OsdRegionId id) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::RegionRecord* region = impl_->Find(id);
    if (region == nullptr) {
        return infra::Status::kNotFound;
    }
    const infra::Status status =
        impl_->mpp.Detach(region->mpp_handle, region->config);
    if (status == infra::Status::kOk) {
        region->attached = false;
    }
    return status;
}

infra::Status OsdService::SetVisible(OsdRegionId id, bool visible) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::RegionRecord* region = impl_->Find(id);
    if (region == nullptr) {
        return infra::Status::kNotFound;
    }
    OsdRegionConfig next_config = region->config;
    next_config.visible = visible;
    const infra::Status status =
        impl_->mpp.SetDisplay(region->mpp_handle, next_config);
    if (status == infra::Status::kOk) {
        region->config = next_config;
    }
    return status;
}

infra::Status OsdService::UpdateBitmap(OsdRegionId id, const OsdBitmap& bitmap) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::RegionRecord* region = impl_->Find(id);
    if (region == nullptr) {
        return infra::Status::kNotFound;
    }
    if (!IsValidBitmap(bitmap)) {
        return infra::Status::kInvalidParam;
    }
    OsdRegionConfig next_config = region->config;
    next_config.size = bitmap.dimensions;
    next_config.pixel_format = bitmap.pixel_format;
    if (!IsValidRegionConfig(next_config)) {
        return infra::Status::kInvalidParam;
    }
    const infra::Status status =
        impl_->mpp.UpdateBitmap(region->mpp_handle, bitmap);
    if (status == infra::Status::kOk) {
        region->has_bitmap = true;
        region->config = next_config;
        ++impl_->stats.bitmap_update_count;
    }
    return status;
}

infra::Status OsdService::DestroyRegion(OsdRegionId id) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto iter = impl_->regions.begin(); iter != impl_->regions.end(); ++iter) {
        if (iter->id.value == id.value) {
            if (iter->attached) {
                (void)impl_->mpp.Detach(iter->mpp_handle, iter->config);
            }
            if (iter->created) {
                impl_->mpp.Destroy(iter->mpp_handle);
            }
            impl_->regions.erase(iter);
            impl_->stats.region_count =
                static_cast<uint32_t>(impl_->regions.size());
            return infra::Status::kOk;
        }
    }
    return infra::Status::kNotFound;
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
