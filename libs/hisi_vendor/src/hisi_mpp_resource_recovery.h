#ifndef LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_RESOURCE_RECOVERY_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_RESOURCE_RECOVERY_H_

#include "media/pipeline_config.h"

namespace live_stream {
namespace hisisdk {
namespace mpp_resource_recovery {

constexpr int kMppExitRetryLimit = 20;

enum class MppExitBusyLog {
    kSilent = 0,
    kInfo,
    kWarn,
};

bool ExitMppSystem(bool log_errors, int retry_limit,
                   MppExitBusyLog busy_log);
void ForceCleanupPipelineResources(const MediaPipelineConfig& config,
                                   bool warn);

}  // namespace mpp_resource_recovery
}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_RESOURCE_RECOVERY_H_
