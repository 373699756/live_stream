#ifndef LIVE_STREAM_DEVICE_SRC_DEVICE_FEATURES_H_
#define LIVE_STREAM_DEVICE_SRC_DEVICE_FEATURES_H_

#include "device.h"
#include "media_channels.h"

#include <memory>

namespace live_stream {
namespace device_internal {

class RegionOverlay;
class Snapshot;

class DeviceFeatures {
public:
    DeviceFeatures(const DeviceMediaOptions& options,
                   const MediaChannels& channels);
    ~DeviceFeatures();

    bool Bind(const MediaChannels& channels);
    bool Start();
    void Stop();
    void Release();

    SnapshotFrame CaptureSnapshot(const SnapshotRequest& request);
    SnapshotInfo GetSnapshotInfo() const;
    OverlayInfo GetOverlayInfo() const;

private:
    std::unique_ptr<Snapshot> snapshot_;
    std::unique_ptr<RegionOverlay> region_overlay_;
};

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_DEVICE_FEATURES_H_
