#include "device_phase.h"

namespace live_stream {
namespace device_internal {

bool CanPrepare(DevicePhase phase) {
    return phase == DevicePhase::kCreated ||
           phase == DevicePhase::kDeinitialized;
}

bool IsPrepared(DevicePhase phase) {
    return phase == DevicePhase::kInitialized ||
           phase == DevicePhase::kStarted ||
           phase == DevicePhase::kStopped;
}

}  // namespace device_internal
}  // namespace live_stream
