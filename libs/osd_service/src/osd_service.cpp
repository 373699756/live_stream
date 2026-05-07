#include "osd_service.h"

#include "config_service.h"
#include "live_stream/json_utils.h"
#include "media_service.h"
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

} // namespace

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
  MediaChannels media_channels{};
  HostOsdMppAdapter mpp;
  uint32_t next_id = 1;
  std::vector<RegionRecord> regions;
  OsdServiceStats stats;
  mutable std::mutex mutex;
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
    std::lock_guard<std::mutex> lock(mutex);
    DestroyAll();
    media_bound = false;
    if (state != ServiceState::kCreated) {
      state = ServiceState::kDeinitialized;
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
    if (!IsValidOsdConfig(value)) {
      return false;
    }
    OsdRegionConfig base;
    base.target =
        media_bound ? media_channels.venc : MppChannel{MppModule::kVenc, 0, 0};
    const ConfigJson &items = value["items"];
    OsdRegionConfig timestamp = base;
    OsdRegionConfig device_name = base;
    if (items.contains("timestamp") &&
        !ParseOsdItem(items, "timestamp", &timestamp)) {
      return false;
    }
    if (items.contains("device_name") &&
        !ParseOsdItem(items, "device_name", &device_name)) {
      return false;
    }
    return true;
  }

  bool UpsertConfigRegion(const std::string &name,
                          const OsdRegionConfig &config) {
    RegionRecord *region = FindByName(name);
    if (region != nullptr) {
      const bool ok = mpp.SetDisplay(region->mpp_handle, config);
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
    if (!mpp.Create(handle, config)) {
      return false;
    }
    if (!mpp.Attach(handle, config)) {
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
    regions.push_back(std::move(record));
    return true;
  }

  bool ApplyConfig(const ConfigJson &value) {
    if (!VerifyConfig(value)) {
      ++stats.config_apply_failed_count;
      return false;
    }
    if (state != ServiceState::kStarted || !media_bound) {
      ++stats.config_apply_count;
      return true;
    }
    bool enabled = false;
    if (!json_utils::Load(value, "enabled", &enabled)) {
      ++stats.config_apply_failed_count;
      return false;
    }
    if (!enabled) {
      DestroyAll();
      ++stats.config_apply_count;
      stats.region_count = static_cast<uint32_t>(regions.size());
      return true;
    }

    const ConfigJson &items = value["items"];
    OsdRegionConfig base;
    base.target = media_channels.venc;
    OsdRegionConfig timestamp = base;
    if (ParseOsdItem(items, "timestamp", &timestamp)) {
      if (!UpsertConfigRegion("timestamp", timestamp)) {
        ++stats.config_apply_failed_count;
        return false;
      }
    }
    OsdRegionConfig device_name = base;
    if (ParseOsdItem(items, "device_name", &device_name)) {
      if (!UpsertConfigRegion("device_name", device_name)) {
        ++stats.config_apply_failed_count;
        return false;
      }
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
    if (impl_->options.media_service == nullptr) {
      return false;
    }
    const MediaChannels channels = impl_->options.media_service->GetChannels();
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

} // namespace live_stream
