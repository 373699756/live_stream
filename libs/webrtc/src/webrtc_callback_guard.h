#ifndef LIVE_STREAM_WEBRTC_SRC_WEBRTC_CALLBACK_GUARD_H_
#define LIVE_STREAM_WEBRTC_SRC_WEBRTC_CALLBACK_GUARD_H_

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace live_stream {

class WebrtcImpl;

struct WebrtcCallbackGuard {
    std::mutex mutex;
    std::condition_variable condition;
    WebrtcImpl *service = nullptr;
    uint32_t active_callbacks = 0;
    bool closing = false;
};

WebrtcImpl *EnterWebrtcCallback(WebrtcCallbackGuard *callback_guard);
void LeaveWebrtcCallback(WebrtcCallbackGuard *callback_guard);
void CloseWebrtcCallbacks(WebrtcCallbackGuard *callback_guard);
void WaitWebrtcCallbacks(WebrtcCallbackGuard *callback_guard);

}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_WEBRTC_CALLBACK_GUARD_H_
