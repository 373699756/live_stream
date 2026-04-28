#include "osd_service.h"

#include "osd_region.h"

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

struct OsdService::Impl {
    struct RegionRecord {
        OsdRegionId id;
        int32_t mpp_handle = -1;
        OsdRegionConfig config;
        bool created = false;
        bool attached = false;
        bool has_bitmap = false;
    };

    ServiceState state = ServiceState::kCreated;
    bool media_bound = false;
    MediaChannels media_channels{};
    HostOsdMppAdapter mpp;
    uint32_t next_id = 1;
    std::vector<RegionRecord> regions;

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
};

OsdService::OsdService() : impl_(new Impl()) {}

OsdService::~OsdService() {
    Deinit();
    delete impl_;
    impl_ = nullptr;
}

infra::Status OsdService::Init() {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    if (impl_->state == ServiceState::kInitialized ||
        impl_->state == ServiceState::kStarted ||
        impl_->state == ServiceState::kStopped) {
        return infra::Status::kOk;
    }
    impl_->state = ServiceState::kInitialized;
    return infra::Status::kOk;
}

infra::Status OsdService::Start() {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
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
    return infra::Status::kOk;
}

void OsdService::Stop() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->DetachAll();
    if (impl_->state == ServiceState::kStarted) {
        impl_->state = ServiceState::kStopped;
    }
}

void OsdService::Deinit() {
    if (impl_ == nullptr) {
        return;
    }
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
    return infra::Result<OsdRegionId>::Ok(impl_->regions.back().id);
}

infra::Status OsdService::Attach(OsdRegionId id) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
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
    }
    return status;
}

infra::Status OsdService::DestroyRegion(OsdRegionId id) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    for (auto iter = impl_->regions.begin(); iter != impl_->regions.end(); ++iter) {
        if (iter->id.value == id.value) {
            if (iter->attached) {
                (void)impl_->mpp.Detach(iter->mpp_handle, iter->config);
            }
            if (iter->created) {
                impl_->mpp.Destroy(iter->mpp_handle);
            }
            impl_->regions.erase(iter);
            return infra::Status::kOk;
        }
    }
    return infra::Status::kNotFound;
}

uint32_t OsdService::RegionCount() const {
    if (impl_ == nullptr) {
        return 0;
    }
    return static_cast<uint32_t>(impl_->regions.size());
}

}  // namespace live_stream
