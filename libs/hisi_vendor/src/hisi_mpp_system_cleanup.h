#ifndef LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_SYSTEM_CLEANUP_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_SYSTEM_CLEANUP_H_

#include "hisi_vendor/media_pipeline.h"

namespace live_stream {
namespace hisisdk {
namespace mpp_system_cleanup {

constexpr int kMppExitRetryLimit = 20;

enum class MppExitBusyLog {
    kSilent = 0,
    kInfo,
    kWarn,
};

bool ExitMppSystem(bool log_errors, int retry_limit,
                   MppExitBusyLog busy_log);
void ForceCleanupPipelineChannels(const MediaPipelineConfig& config,
                                  bool warn);

}  // namespace mpp_system_cleanup
}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_SYSTEM_CLEANUP_H_
