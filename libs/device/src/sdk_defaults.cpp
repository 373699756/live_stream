#include "sdk_defaults.h"

#include "host_hisi_sdk.h"

namespace live_stream {
namespace device_internal {

hisisdk::HisiSdk FillSdkDefaults(hisisdk::HisiSdk sdk) {
    const hisisdk::HisiSdk host_sdk = HostHisiSdk();
    if (sdk.system == nullptr) {
        sdk.system = host_sdk.system;
    }
    if (sdk.media_pipeline == nullptr) {
        sdk.media_pipeline = host_sdk.media_pipeline;
    }
    if (sdk.venc_stream == nullptr) {
        sdk.venc_stream = host_sdk.venc_stream;
    }
    if (sdk.region == nullptr) {
        sdk.region = host_sdk.region;
    }
    if (sdk.snapshot == nullptr) {
        sdk.snapshot = host_sdk.snapshot;
    }
    if (sdk.image == nullptr) {
        sdk.image = host_sdk.image;
    }
    return sdk;
}

DeviceMediaOptions FillDeviceDefaults(DeviceMediaOptions options) {
    options.sdk = FillSdkDefaults(options.sdk);
    return options;
}

}  // namespace device_internal
}  // namespace live_stream
