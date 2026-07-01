#ifndef LIVE_STREAM_SYSTEM_UPGRADE_H_
#define LIVE_STREAM_SYSTEM_UPGRADE_H_

#include "event.h"
#include "request_context.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace live_stream {

class IConfig;
class ILogger;

class Upgrade {
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

struct UpgradeInfo {
    UpgradeState state = UpgradeState::kIdle;
    uint32_t progress_percent = 0;
    std::string current_stage = "idle";
    std::string target_version;
    bool ok = true;
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

    virtual UpgradePackageInfo ValidatePackage(
        const std::string& package_path) = 0;
    virtual std::string GetCurrentVersion() = 0;
    // Returns < 0 when lhs is older than rhs, 0 when equal, > 0 when newer.
    virtual int CompareVersion(const std::string& lhs,
                               const std::string& rhs) = 0;
    virtual bool PrepareUpgrade(const UpgradePackageInfo& info) = 0;
    virtual bool WriteUpgrade(
        const std::string& package_path,
        UpgradeProgressCallback progress_callback) = 0;
    virtual void SetAutoRebootPolicy(bool /*auto_reboot*/) {}
    virtual bool IsExternalFlashWriterActive() const = 0;
    virtual bool CommitUpgrade(const UpgradePackageInfo& info) = 0;
    virtual bool CancelUpgrade() = 0;
    virtual bool RebootToApply() = 0;
    virtual bool CleanupFailedUpgrade() = 0;
    virtual std::string LastError() = 0;
};

struct UpgradeOptions {
    IConfig* config = nullptr;
    event::EventCenter* event = nullptr;
    ILogger* logger = nullptr;
    IUpgradePlatform* platform = nullptr;
    uint64_t max_package_size_bytes = 32ULL * 1024ULL * 1024ULL;
    uint32_t max_package_path_length = 512;
    uint32_t queue_capacity = 8;
};

class IUpgrade {
public:
    virtual ~IUpgrade() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsStarted() const = 0;
    virtual UpgradeInfo GetUpgradeInfo() = 0;
    virtual UpgradePackageInfo ValidatePackage(
        const std::string& package_path) = 0;
    virtual std::string LastError() = 0;
    virtual bool StartUpgrade(const live_stream::RequestContext& context,
                              const UpgradeRequest& request) = 0;
    virtual bool CancelUpgrade(
        const live_stream::RequestContext& context) = 0;
    virtual bool ConfirmReboot(
        const live_stream::RequestContext& context) = 0;
};

std::unique_ptr<IUpgrade> CreateUpgrade(
    const UpgradeOptions& options);

const char* UpgradeStateToString(UpgradeState state);

}  // namespace live_stream

#endif  // LIVE_STREAM_SYSTEM_UPGRADE_H_
