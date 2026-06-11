#ifndef LIVE_STREAM_DEVICE_SRC_LIFECYCLE_STATE_H_
#define LIVE_STREAM_DEVICE_SRC_LIFECYCLE_STATE_H_

namespace live_stream {
namespace device_internal {

enum class LifecycleState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopping,
    kStopped,
    kDeinitialized,
    kFailed,
};

inline bool CanPrepare(LifecycleState state) {
    return state == LifecycleState::kCreated ||
           state == LifecycleState::kDeinitialized;
}

inline bool IsPrepared(LifecycleState state) {
    return state == LifecycleState::kInitialized ||
           state == LifecycleState::kStarted ||
           state == LifecycleState::kStopped;
}

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_LIFECYCLE_STATE_H_
