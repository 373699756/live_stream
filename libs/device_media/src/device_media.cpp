#include "device_media.h"

#include "device_media_runtime.h"

namespace live_stream {

std::unique_ptr<IDeviceMedia> CreateDeviceMedia() {
    return CreateDeviceMedia(DeviceMediaOptions{});
}

std::unique_ptr<IDeviceMedia> CreateDeviceMedia(
    const MediaPipelineConfig &config) {
    DeviceMediaOptions options;
    options.default_config = config;
    return CreateDeviceMedia(options);
}

std::unique_ptr<IDeviceMedia> CreateDeviceMedia(
    const DeviceMediaOptions &options) {
    return device_media_internal::CreateDeviceMediaCore(options);
}

}  // namespace live_stream
