#ifndef LIVE_STREAM_OSD_SERVICE_SRC_OSD_REGION_H_
#define LIVE_STREAM_OSD_SERVICE_SRC_OSD_REGION_H_

#include "osd_service.h"

namespace live_stream {

namespace hisisdk {
class IHisiSdk;
}  // namespace hisisdk

namespace osd_internal {

constexpr uint32_t kMaxRegions = 16;

bool IsValidChannel(const MppChannel& channel);
bool IsValidRegionConfig(const OsdRegionConfig& config);
bool IsValidBitmap(const OsdBitmap& bitmap);
int32_t MinHandle(OsdRegionType type);

class OsdMppAdapter {
 public:
    virtual ~OsdMppAdapter() = default;

    virtual infra::Status Create(int32_t handle,
                                 const OsdRegionConfig& config) = 0;
    virtual infra::Status Attach(int32_t handle,
                                 const OsdRegionConfig& config) = 0;
    virtual infra::Status Detach(int32_t handle,
                                 const OsdRegionConfig& config) = 0;
    virtual infra::Status SetDisplay(int32_t handle,
                                     const OsdRegionConfig& config) = 0;
    virtual infra::Status UpdateBitmap(int32_t handle,
                                       const OsdBitmap& bitmap) = 0;
    virtual void Destroy(int32_t handle) = 0;
};

class HostOsdMppAdapter final : public OsdMppAdapter {
 public:
    explicit HostOsdMppAdapter(hisisdk::IHisiSdk* sdk = nullptr);

    infra::Status Create(int32_t handle,
                         const OsdRegionConfig& config) override;
    infra::Status Attach(int32_t handle,
                         const OsdRegionConfig& config) override;
    infra::Status Detach(int32_t handle,
                         const OsdRegionConfig& config) override;
    infra::Status SetDisplay(int32_t handle,
                             const OsdRegionConfig& config) override;
    infra::Status UpdateBitmap(int32_t handle,
                               const OsdBitmap& bitmap) override;
    void Destroy(int32_t handle) override;

 private:
    hisisdk::IHisiSdk* sdk_;
};

}  // namespace osd_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_OSD_SERVICE_SRC_OSD_REGION_H_
