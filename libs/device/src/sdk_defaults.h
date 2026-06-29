#ifndef LIVE_STREAM_DEVICE_SRC_SDK_DEFAULTS_H_
#define LIVE_STREAM_DEVICE_SRC_SDK_DEFAULTS_H_

#include "device.h"

namespace live_stream {
namespace device_internal {

hisisdk::HisiSdk FillSdkDefaults(hisisdk::HisiSdk sdk);
DeviceMediaOptions FillDeviceDefaults(DeviceMediaOptions options);

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_SDK_DEFAULTS_H_
