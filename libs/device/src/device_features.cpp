#include "device_features.h"

#include "region_overlay.h"
#include "snapshot.h"

namespace live_stream {
namespace device_internal {

DeviceFeatures::DeviceFeatures(const DeviceMediaOptions& options,
                               const MediaChannels& channels) {
    SnapshotConfig snapshot_config;
    snapshot_config.jpeg_venc_channel = options.snapshot_venc_channel;
    SnapshotOptions snapshot_options;
    snapshot_options.default_config = snapshot_config;
    snapshot_options.config = options.config;
    snapshot_options.media_channels = channels;
    snapshot_options.snapshot = options.sdk.snapshot;
    snapshot_.reset(new Snapshot(snapshot_options));

    RegionOverlayOptions overlay_options;
    overlay_options.config = options.config;
    overlay_options.media_channels = channels;
    overlay_options.region = options.sdk.region;
    region_overlay_.reset(new RegionOverlay(overlay_options));
}

DeviceFeatures::~DeviceFeatures() = default;

bool DeviceFeatures::Bind(const MediaChannels& channels) {
    if (!snapshot_->BindMedia(channels)) {
        return false;
    }
    if (!region_overlay_->BindMedia(channels)) {
        return false;
    }
    return true;
}

bool DeviceFeatures::Start() {
    if (!snapshot_->Start()) {
        return false;
    }
    if (!region_overlay_->Start()) {
        snapshot_->Stop();
        return false;
    }
    return true;
}

void DeviceFeatures::Stop() {
    region_overlay_->Stop();
    snapshot_->Stop();
}

void DeviceFeatures::Release() {
    region_overlay_->Release();
    snapshot_->Release();
}

SnapshotFrame DeviceFeatures::CaptureSnapshot(
    const SnapshotRequest& request) {
    return snapshot_->Capture(request);
}

SnapshotInfo DeviceFeatures::GetSnapshotInfo() const {
    return snapshot_->GetInfo();
}

OverlayInfo DeviceFeatures::GetOverlayInfo() const {
    return region_overlay_->GetInfo();
}

}  // namespace device_internal
}  // namespace live_stream
