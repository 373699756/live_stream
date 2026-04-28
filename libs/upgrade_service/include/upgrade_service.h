#ifndef LIVE_STREAM_UPGRADE_SERVICE_H_
#define LIVE_STREAM_UPGRADE_SERVICE_H_

#include "infra/request_context.h"
#include "infra/status.h"
#include "infra/service.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace live_stream {

class IConfigService;
class IEventService;
class ILoggerService;

class UpgradeService {
 public:
    static const char* Name();
};

enum class UpgradeState {
    kIdle,
    kValidating,
    kPreparing,
    kWriting,
    kCommitting,
    kWaitingReboot,
    kCompleted,
    kFailed,
    kCanceled,
};

struct UpgradePackageInfo {
    std::string package_path;
    std::string version;
    uint64_t size_bytes = 0;
    std::string digest;
    int64_t build_time_ms = 0;
    std::string target_model;
    bool requires_reboot = true;
};

struct UpgradeStatus {
    UpgradeState state = UpgradeState::kIdle;
    uint32_t progress_percent = 0;
    std::string current_stage = "idle";
    std::string target_version;
    infra::Status status = infra::Status::kOk;
    std::string error_message;
    int64_t started_at_ms = 0;
    int64_t finished_at_ms = 0;
};

struct UpgradeRequest {
    std::string package_path;
    std::string expected_version;
    bool allow_same_version = false;
    bool allow_downgrade = false;
    bool auto_reboot = false;
};

using UpgradeProgressCallback = std::function<void(uint32_t progress_percent)>;

class IUpgradePlatform {
 public:
    virtual ~IUpgradePlatform() = default;

    virtual infra::Result<UpgradePackageInfo> ValidatePackage(
        const std::string& package_path) = 0;
    virtual infra::Result<std::string> GetCurrentVersion() = 0;
    // Returns < 0 when lhs is older than rhs, 0 when equal, > 0 when newer.
    virtual infra::Result<int> CompareVersion(const std::string& lhs,
                                              const std::string& rhs) = 0;
    virtual infra::Status PrepareUpgrade(const UpgradePackageInfo& info) = 0;
    virtual infra::Status WriteUpgrade(
        const std::string& package_path,
        UpgradeProgressCallback progress_callback) = 0;
    virtual infra::Status CommitUpgrade(const UpgradePackageInfo& info) = 0;
    virtual infra::Status CancelUpgrade() = 0;
    virtual infra::Status RebootToApply() = 0;
    virtual infra::Status CleanupFailedUpgrade() = 0;
};

struct UpgradeServiceOptions {
    IConfigService* config_service = nullptr;
    IEventService* event_service = nullptr;
    ILoggerService* logger_service = nullptr;
    IUpgradePlatform* platform = nullptr;
    uint64_t max_package_size_bytes = 256ULL * 1024ULL * 1024ULL;
    uint32_t max_package_path_length = 512;
    uint32_t queue_capacity = 8;
};

class IUpgradeService : public infra::IService {
 public:
    virtual UpgradeStatus GetStatus() = 0;
    virtual infra::Result<UpgradePackageInfo> ValidatePackage(
        const std::string& package_path) = 0;
    virtual infra::Status StartUpgrade(const infra::RequestContext& context,
                                      const UpgradeRequest& request) = 0;
    virtual infra::Status CancelUpgrade(
        const infra::RequestContext& context) = 0;
    virtual infra::Status ConfirmReboot(
        const infra::RequestContext& context) = 0;
};

std::unique_ptr<IUpgradeService> CreateUpgradeService(
    const UpgradeServiceOptions& options);

const char* UpgradeStateToString(UpgradeState state);

}  // namespace live_stream

#endif  // LIVE_STREAM_UPGRADE_SERVICE_H_
