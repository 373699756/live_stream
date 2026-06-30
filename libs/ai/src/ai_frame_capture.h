#ifndef LIVE_STREAM_AI_SRC_AI_FRAME_CAPTURE_H_
#define LIVE_STREAM_AI_SRC_AI_FRAME_CAPTURE_H_

#include "ai.h"
#include "hisi_vendor/sdk.h"

#include <mutex>

namespace live_stream {
class DeviceMedia;

namespace ai_internal {

class AiFrameCapture final {
public:
    AiFrameCapture(hisisdk::IHisiSnapshot *snapshot,
                   DeviceMedia *device);

    bool Available() const;
    hisisdk::YuvFrame Capture(const AiModelConfig &config);

private:
    hisisdk::IHisiSnapshot *snapshot_ = nullptr;
    DeviceMedia *device_ = nullptr;
    std::mutex mutex_;
};

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_FRAME_CAPTURE_H_
