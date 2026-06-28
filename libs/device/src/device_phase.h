#ifndef LIVE_STREAM_DEVICE_SRC_DEVICE_PHASE_H_
#define LIVE_STREAM_DEVICE_SRC_DEVICE_PHASE_H_

namespace live_stream {
namespace device_internal {

enum class DevicePhase {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopping,
    kStopped,
    kDeinitialized,
    kFailed,
};

bool CanPrepare(DevicePhase phase);
bool IsPrepared(DevicePhase phase);

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_DEVICE_PHASE_H_
