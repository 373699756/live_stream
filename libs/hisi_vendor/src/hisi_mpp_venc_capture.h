#ifndef LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VENC_CAPTURE_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VENC_CAPTURE_H_

#include "hisi_vendor/mpp_sdk.h"

#include <atomic>

namespace live_stream {
namespace hisisdk {
namespace venc_internal {

class VencStreamCapture {
public:
    static void Run(MediaPipelineConfig config,
                    MediaFrameCallback callback,
                    void* user,
                    std::atomic<bool>& running);
};

}  // namespace venc_internal
}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_VENC_CAPTURE_H_
