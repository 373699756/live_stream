#include "upgrade.h"

#include "event.h"
#include "infra/clamp.h"
#include "infra/time.h"
#include "logger.h"

#include <limits.h>
#include <mutex>
#include <sys/stat.h>
#include <utility>
#include <unistd.h>

namespace live_stream {
namespace {

const char* kServiceName = "upgrade";
constexpr const char* kUpgradeUploadDir = "/tmp/live_stream/upgrade/uploads";

bool IsTerminalState(UpgradeState state) {
    return state == UpgradeState::kIdle || state == UpgradeState::kCompleted ||
           state == UpgradeState::kFailed || state == UpgradeState::kCanceled;
}

bool IsCancelableState(UpgradeState state) {
    return state == UpgradeState::kValidating ||
           state == UpgradeState::kPreparing ||
           state == UpgradeState::kWriting;
}

OperationResult ToOperationResult(bool ok) {
    return ok ? OperationResult::kSuccess : OperationResult::kFailed;
}

class RestrictedUpgradePlatform : public IUpgradePlatform {
public:
    UpgradePackageInfo ValidatePackage(
        const std::string& package_path) override {
        (void)package_path;
        return UpgradePackageInfo();
    }

    std::string GetCurrentVersion() override {
        return std::string();
    }

    int CompareVersion(const std::string& lhs,
                       const std::string& rhs) override {
        (void)lhs;
        (void)rhs;
        return 0;
    }

    bool PrepareUpgrade(const UpgradePackageInfo& info) override {
        (void)info;
        return false;
    }

    bool WriteUpgrade(
        const std::string& package_path,
        UpgradeProgressCallback progress_callback) override {
        (void)package_path;
        (void)progress_callback;
        return false;
    }

    bool CommitUpgrade(const UpgradePackageInfo& info) override {
        (void)info;
        return false;
    }

    bool CancelUpgrade() override {
        return false;
    }

    bool RebootToApply() override {
        return false;
    }

    bool CleanupFailedUpgrade() override {
        return true;
    }
};

class UpgradeImpl : public IUpgrade {
public:
    explicit UpgradeImpl(UpgradeOptions options)
        : options_(std::move(options)) {}

    ~UpgradeImpl() override {
        ReleaseInternal();
    }

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return true;
        }
        if (options_.max_package_size_bytes == 0 ||
            options_.max_package_path_length == 0 ||
            options_.queue_capacity == 0) {
            return false;
        }
        if (options_.platform == nullptr) {
            restricted_platform_.reset(new RestrictedUpgradePlatform());
            platform_ = restricted_platform_.get();
        } else {
            platform_ = options_.platform;
        }

        executor_.reset(new event::Executor());

        status_ = UpgradeStatus{};
        initialized_ = true;
        return true;
    }

    bool Start() override {
        if (!Prepare()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!executor_) {
            return false;
        }
        if (started_) {
            return true;
        }
        event::ExecutorOptions executor_options;
        executor_options.worker_count = 1;
        executor_options.queue_capacity = options_.queue_capacity;
        if (!executor_->Start(executor_options)) {
            return false;
        }
        started_ = true;
        return true;
    }

    void Stop() override {
        StopInternal();
    }

    bool IsStarted() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
    }

    void Release() {
        ReleaseInternal();
    }

private:
    void StopInternal() {
        std::unique_ptr<event::Executor> executor;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = false;
            executor_.swap(executor);
        }
        if (executor) {
            executor->Stop(event::StopMode::kDiscard);
            std::lock_guard<std::mutex> lock(mutex_);
            if (!executor_) {
                executor_.swap(executor);
            }
        }
    }

    void ReleaseInternal() {
        StopInternal();
        std::lock_guard<std::mutex> lock(mutex_);
        executor_.reset();
        restricted_platform_.reset();
        platform_ = nullptr;
        status_ = UpgradeStatus{};
        cancel_requested_ = false;
        initialized_ = false;
    }

public:
    UpgradeStatus GetStatus() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

    UpgradePackageInfo ValidatePackage(
        const std::string& package_path) override {
        std::string reason;
        std::string checked_package_path;
        if (!ValidateLocalPackage(package_path, &checked_package_path,
                                  &reason)) {
            return UpgradePackageInfo();
        }
        IUpgradePlatform* platform = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || platform_ == nullptr) {
                return UpgradePackageInfo();
            }
            platform = platform_;
        }
        return platform->ValidatePackage(checked_package_path);
    }

    bool StartUpgrade(const live_stream::RequestContext& context,
                      const UpgradeRequest& request) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_ || !executor_) {
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "service not started");
                return false;
            }
            if (!IsTerminalState(status_.state)) {
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "upgrade busy");
                return false;
            }
        }

        std::string reason;
        std::string checked_package_path;
        if (!ValidateLocalPackage(request.package_path, &checked_package_path,
                                  &reason)) {
            RecordAudit(context, request.package_path, OperationResult::kRejected,
                        reason);
            return false;
        }

        event::Executor* executor = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_ || !executor_) {
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "service not started");
                return false;
            }
            if (!IsTerminalState(status_.state)) {
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "upgrade busy");
                return false;
            }
            cancel_requested_ = false;
            status_ = UpgradeStatus{};
            status_.state = UpgradeState::kValidating;
            status_.progress_percent = 0;
            status_.current_stage = UpgradeStateToString(UpgradeState::kValidating);
            status_.ok = true;
            status_.started_at_ms = infra::Time::SystemTimeMillis();
            executor = executor_.get();
        }
        PublishProgressChanged();

        UpgradeRequest checked_request = request;
        checked_request.package_path = checked_package_path;
        if (executor->Post([this, context, checked_request]() {
                ExecuteUpgrade(context, checked_request);
            }) != event::EventStatus::kOk) {
            SetFailed("failed to queue upgrade task", false);
            RecordAudit(context, request.package_path, OperationResult::kRejected,
                        "failed to queue upgrade task");
            return false;
        }
        return true;
    }

    bool CancelUpgrade(const live_stream::RequestContext& context) override {
        IUpgradePlatform* platform = nullptr;
        std::string target;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_ || platform_ == nullptr) {
                RecordAudit(context, "upgrade", OperationResult::kRejected,
                            "service not started");
                return false;
            }
            if (!IsCancelableState(status_.state)) {
                RecordAudit(context, status_.target_version,
                            OperationResult::kRejected, "upgrade not cancelable");
                return false;
            }
            cancel_requested_ = true;
            target = status_.target_version;
            status_.state = UpgradeState::kCanceled;
            status_.current_stage = UpgradeStateToString(UpgradeState::kCanceled);
            status_.ok = true;
            status_.error_message = "canceled";
            status_.finished_at_ms = infra::Time::SystemTimeMillis();
            platform = platform_;
        }
        const bool cancel_ok = platform->CancelUpgrade();
        PublishProgressChanged();
        RecordAudit(context, target, ToOperationResult(cancel_ok), "canceled");
        return cancel_ok;
    }

    bool ConfirmReboot(const live_stream::RequestContext& context) override {
        IUpgradePlatform* platform = nullptr;
        std::string target;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_ || platform_ == nullptr) {
                RecordAudit(context, "upgrade", OperationResult::kRejected,
                            "service not started");
                return false;
            }
            if (status_.state != UpgradeState::kWaitingReboot) {
                RecordAudit(context, status_.target_version,
                            OperationResult::kRejected, "reboot not pending");
                return false;
            }
            platform = platform_;
            target = status_.target_version;
        }

        const bool reboot_ok = platform->RebootToApply();
        if (reboot_ok) {
            UpdateStatus(UpgradeState::kCompleted, 100, true, "");
        } else {
            SetFailed("reboot failed", true);
        }
        RecordAudit(context, target, ToOperationResult(reboot_ok),
                    reboot_ok ? "ok" : "reboot failed");
        return reboot_ok;
    }

private:
    bool ValidateLocalPackage(const std::string& package_path,
                              std::string* checked_package_path,
                              std::string* reason) {
        if (checked_package_path == nullptr) {
            return false;
        }
        checked_package_path->clear();
        if (package_path.empty() ||
            package_path.size() > options_.max_package_path_length) {
            if (reason != nullptr) {
                *reason = "invalid package path";
            }
            return false;
        }
        struct stat link_stat;
        if (lstat(package_path.c_str(), &link_stat) != 0 ||
            S_ISLNK(link_stat.st_mode)) {
            if (reason != nullptr) {
                *reason = "package path is not allowed";
            }
            return false;
        }
        char resolved_path[PATH_MAX] = {0};
        if (realpath(package_path.c_str(), resolved_path) == nullptr) {
            if (reason != nullptr) {
                *reason = "package path is not allowed";
            }
            return false;
        }
        const std::string resolved_package_path(resolved_path);
        const std::string upload_prefix =
            std::string(kUpgradeUploadDir) + "/";
        if (resolved_package_path.compare(0, upload_prefix.size(),
                                          upload_prefix) != 0) {
            if (reason != nullptr) {
                *reason = "package path is outside upload directory";
            }
            return false;
        }

        struct stat file_stat;
        if (stat(resolved_package_path.c_str(), &file_stat) != 0) {
            if (reason != nullptr) {
                *reason = "package not found";
            }
            return false;
        }
        if (!S_ISREG(file_stat.st_mode)) {
            if (reason != nullptr) {
                *reason = "package path is not a regular file";
            }
            return false;
        }
        if (file_stat.st_size <= 0) {
            if (reason != nullptr) {
                *reason = "package is empty";
            }
            return false;
        }
        if (static_cast<uint64_t>(file_stat.st_size) >
            options_.max_package_size_bytes) {
            if (reason != nullptr) {
                *reason = "package too large";
            }
            return false;
        }
        *checked_package_path = resolved_package_path;
        if (reason != nullptr) {
            reason->clear();
        }
        return true;
    }

    void ExecuteUpgrade(const live_stream::RequestContext& context,
                        const UpgradeRequest& request) {
        IUpgradePlatform* platform = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            platform = platform_;
        }
        if (platform == nullptr) {
            SetFailed("platform missing", false);
            RecordAudit(context, request.package_path, OperationResult::kFailed,
                        "platform missing");
            return;
        }

        UpgradePackageInfo info =
            platform->ValidatePackage(request.package_path);
        if (info.version.empty()) {
            SetFailed("package validation failed", false);
            RecordAudit(context, request.package_path, OperationResult::kFailed,
                        "package validation failed");
            return;
        }
        if (info.package_path.empty()) {
            info.package_path = request.package_path;
        }

        std::string policy_reason;
        if (!ValidateVersionPolicy(platform, info, request, &policy_reason)) {
            SetFailed(policy_reason, false);
            RecordAudit(context, info.version, OperationResult::kRejected,
                        policy_reason);
            return;
        }

        if (IsCancelRequested()) {
            return;
        }
        UpdateTargetVersion(info.version);
        UpdateStatus(UpgradeState::kPreparing, 10, true, "");
        if (!platform->PrepareUpgrade(info)) {
            static_cast<void>(platform->CleanupFailedUpgrade());
            SetFailed("prepare upgrade failed", false);
            RecordAudit(context, info.version, OperationResult::kFailed,
                        "prepare upgrade failed");
            return;
        }

        if (IsCancelRequested()) {
            return;
        }
        UpdateStatus(UpgradeState::kWriting, 20, true, "");
        const bool write_ok = platform->WriteUpgrade(
            request.package_path, [this](uint32_t progress_percent) {
                const uint32_t bounded =
                    infra::Clamp<uint32_t>(progress_percent, 0U, 100U);
                const uint32_t service_progress = 20U + (bounded * 60U) / 100U;
                UpdateWritingProgress(service_progress);
            });
        if (!write_ok) {
            if (!IsCancelRequested()) {
                static_cast<void>(platform->CleanupFailedUpgrade());
                SetFailed("write upgrade failed", false);
                RecordAudit(context, info.version, OperationResult::kFailed,
                            "write upgrade failed");
            }
            return;
        }

        if (IsCancelRequested()) {
            return;
        }
        UpdateStatus(UpgradeState::kCommitting, 90, true, "");
        if (!platform->CommitUpgrade(info)) {
            SetFailed("commit upgrade failed", true);
            RecordAudit(context, info.version, OperationResult::kFailed,
                        "commit upgrade failed");
            return;
        }

        if (info.requires_reboot) {
            if (request.auto_reboot) {
                if (!platform->RebootToApply()) {
                    SetFailed("reboot failed", true);
                    RecordAudit(context, info.version, OperationResult::kFailed,
                                "reboot failed");
                    return;
                }
                UpdateStatus(UpgradeState::kCompleted, 100, true, "");
                RecordAudit(context, info.version, OperationResult::kSuccess,
                            "completed");
                return;
            }
            UpdateStatus(UpgradeState::kWaitingReboot, 100, true, "");
            RecordAudit(context, info.version, OperationResult::kSuccess,
                        "waiting reboot");
            return;
        }

        UpdateStatus(UpgradeState::kCompleted, 100, true, "");
        RecordAudit(context, info.version, OperationResult::kSuccess, "completed");
    }

    bool ValidateVersionPolicy(IUpgradePlatform* platform,
                               const UpgradePackageInfo& info,
                               const UpgradeRequest& request,
                               std::string* reason) {
        if (!request.expected_version.empty() &&
            request.expected_version != info.version) {
            if (reason != nullptr) {
                *reason = "unexpected package version";
            }
            return false;
        }

        const std::string current_version = platform->GetCurrentVersion();
        if (current_version.empty()) {
            if (reason != nullptr) {
                *reason = "current version unavailable";
            }
            return false;
        }
        const int compare = platform->CompareVersion(info.version, current_version);
        if (compare == 0 && !request.allow_same_version) {
            if (reason != nullptr) {
                *reason = "same version is not allowed";
            }
            return false;
        }
        if (compare < 0 && !request.allow_downgrade) {
            if (reason != nullptr) {
                *reason = "downgrade is not allowed";
            }
            return false;
        }
        if (reason != nullptr) {
            reason->clear();
        }
        return true;
    }

    bool IsCancelRequested() {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancel_requested_;
    }

    void UpdateTargetVersion(const std::string& version) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.target_version = version;
    }

    void UpdateWritingProgress(uint32_t progress_percent) {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (status_.state != UpgradeState::kWriting || cancel_requested_) {
                return;
            }
            progress_percent =
                infra::Clamp<uint32_t>(progress_percent, 0U, 89U);
            if (progress_percent > status_.progress_percent) {
                status_.progress_percent = progress_percent;
                changed = true;
            }
        }
        if (changed) {
            PublishProgressChanged();
        }
    }

    void UpdateStatus(UpgradeState state,
                      uint32_t progress_percent,
                      bool ok,
                      const std::string& error_message) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.state = state;
            status_.progress_percent =
                infra::Clamp<uint32_t>(progress_percent, 0U, 100U);
            status_.current_stage = UpgradeStateToString(state);
            status_.ok = ok;
            status_.error_message = error_message;
            if (state == UpgradeState::kCompleted || state == UpgradeState::kFailed ||
                state == UpgradeState::kCanceled) {
                status_.finished_at_ms = infra::Time::SystemTimeMillis();
            }
        }
        PublishProgressChanged();
    }

    void SetFailed(const std::string& message,
                   bool committed) {
        std::string full_message = message;
        if (committed && !full_message.empty()) {
            full_message += "; committed upgrade may require manual recovery";
        } else if (committed) {
            full_message = "committed upgrade may require manual recovery";
        }
        UpdateStatus(UpgradeState::kFailed, 100, false, full_message);
    }

    void PublishProgressChanged() {
        event::Dispatcher* event_bus = options_.event;
        if (event_bus == nullptr) {
            return;
        }
        UpgradeStatus status = GetStatus();
        event::Event progress_event;
        progress_event.type = event::EventType::kUpgradeProgressChanged;
        progress_event.source = kServiceName;
        progress_event.message = status.current_stage;
        progress_event.value = static_cast<int32_t>(status.progress_percent);
        static_cast<void>(event_bus->Publish(progress_event));
    }

    void RecordAudit(const live_stream::RequestContext& context,
                     const std::string& target,
                     OperationResult result,
                     const std::string& reason) {
        ILogger* logger = options_.logger;
        if (logger == nullptr) {
            return;
        }
        OperationRecord record;
        record.timestamp_ms = infra::Time::SystemTimeMillis();
        record.request_id = context.request_id;
        record.user_name = context.user_name;
        record.session_id = context.session_id;
        record.client_ip = context.client_ip;
        record.module = kServiceName;
        record.action = OperationAction::kUpgrade;
        record.target = target;
        record.result = result;
        record.reason = reason;
        static_cast<void>(logger->RecordOperation(record));
    }

    UpgradeOptions options_;
    std::unique_ptr<event::Executor> executor_;
    std::unique_ptr<IUpgradePlatform> restricted_platform_;
    IUpgradePlatform* platform_ = nullptr;
    mutable std::mutex mutex_;
    UpgradeStatus status_;
    bool initialized_ = false;
    bool started_ = false;
    bool cancel_requested_ = false;
};

}  // namespace

const char* Upgrade::Name() {
    return kServiceName;
}

const char* UpgradeStateToString(UpgradeState state) {
    switch (state) {
        case UpgradeState::kIdle:
            return "idle";
        case UpgradeState::kValidating:
            return "validating";
        case UpgradeState::kPreparing:
            return "preparing";
        case UpgradeState::kWriting:
            return "writing";
        case UpgradeState::kCommitting:
            return "committing";
        case UpgradeState::kWaitingReboot:
            return "waiting_reboot";
        case UpgradeState::kCompleted:
            return "completed";
        case UpgradeState::kFailed:
            return "failed";
        case UpgradeState::kCanceled:
            return "canceled";
    }
    return "unknown";
}

std::unique_ptr<IUpgrade> CreateUpgrade(
    const UpgradeOptions& options) {
    return std::unique_ptr<IUpgrade>(new UpgradeImpl(options));
}

}  // namespace live_stream
