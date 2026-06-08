#ifndef LIVE_STREAM_DEVICE_MEDIA_SRC_DM_STATE_H_
#define LIVE_STREAM_DEVICE_MEDIA_SRC_DM_STATE_H_

namespace live_stream {
namespace device_media_internal {

enum class DeviceMediaState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopping,
    kStopped,
    kDeinitialized,
};

inline bool DeviceMediaCanPrepare(DeviceMediaState state) {
    return state == DeviceMediaState::kCreated ||
           state == DeviceMediaState::kDeinitialized;
}

inline bool DeviceMediaPrepared(DeviceMediaState state) {
    return state == DeviceMediaState::kInitialized ||
           state == DeviceMediaState::kStarted ||
           state == DeviceMediaState::kStopped;
}

}  // namespace device_media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_MEDIA_SRC_DM_STATE_H_
