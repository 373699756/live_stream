#ifndef LIVE_STREAM_DEVICE_SRC_DEVICE_IMPL_H_
#define LIVE_STREAM_DEVICE_SRC_DEVICE_IMPL_H_

#include "device.h"

#include <memory>

namespace live_stream {
namespace device_internal {

std::unique_ptr<DeviceMedia> CreateDeviceMediaCore(
    const DeviceMediaOptions &options);

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_DEVICE_IMPL_H_
