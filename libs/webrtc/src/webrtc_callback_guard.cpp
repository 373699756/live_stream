#include "webrtc_callback_guard.h"

namespace live_stream {

WebrtcImpl *EnterWebrtcCallback(WebrtcCallbackGuard *callback_guard) {
    if (callback_guard == nullptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> guard(callback_guard->mutex);
    if (callback_guard->closing || callback_guard->service == nullptr) {
        return nullptr;
    }
    ++callback_guard->active_callbacks;
    return callback_guard->service;
}

void LeaveWebrtcCallback(WebrtcCallbackGuard *callback_guard) {
    if (callback_guard == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> guard(callback_guard->mutex);
    if (callback_guard->active_callbacks == 0) {
        return;
    }
    --callback_guard->active_callbacks;
    if (callback_guard->active_callbacks == 0) {
        callback_guard->condition.notify_all();
    }
}

void CloseWebrtcCallbacks(WebrtcCallbackGuard *callback_guard) {
    if (callback_guard == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> guard(callback_guard->mutex);
    callback_guard->closing = true;
    callback_guard->service = nullptr;
}

void WaitWebrtcCallbacks(WebrtcCallbackGuard *callback_guard) {
    if (callback_guard == nullptr) {
        return;
    }
    std::unique_lock<std::mutex> guard(callback_guard->mutex);
    callback_guard->condition.wait(guard, [callback_guard]() {
        return callback_guard->active_callbacks == 0;
    });
}

}  // namespace live_stream
