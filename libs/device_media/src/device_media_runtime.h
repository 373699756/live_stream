#ifndef LIVE_STREAM_DEVICE_MEDIA_SRC_DEVICE_MEDIA_RUNTIME_H_
#define LIVE_STREAM_DEVICE_MEDIA_SRC_DEVICE_MEDIA_RUNTIME_H_

#include "device_media.h"

#include <memory>

namespace live_stream {
namespace device_media_internal {

std::unique_ptr<IDeviceMedia> CreateDeviceMediaCore(
    const DeviceMediaOptions &options);

}  // namespace device_media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_MEDIA_SRC_DEVICE_MEDIA_RUNTIME_H_
