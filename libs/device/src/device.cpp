#include "device.h"

#include "device_impl.h"

namespace live_stream {

std::unique_ptr<DeviceMedia> CreateDeviceMedia() {
    return CreateDeviceMedia(DeviceMediaOptions{});
}

std::unique_ptr<DeviceMedia> CreateDeviceMedia(
    const MediaPipelineConfig &config) {
    DeviceMediaOptions options;
    options.default_config = config;
    return CreateDeviceMedia(options);
}

std::unique_ptr<DeviceMedia> CreateDeviceMedia(
    const DeviceMediaOptions &options) {
    return device_internal::CreateDeviceMediaCore(options);
}

}  // namespace live_stream
