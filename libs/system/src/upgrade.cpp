#include "system/upgrade.h"

#include "event.h"
#include "json.h"
#include "infra/clamp.h"
#include "infra/fs.h"
#include "infra/log.h"
#include "infra/time.h"
#include "logger.h"

#include <cstdio>
#include <limits.h>
#include <mutex>
#include <sys/stat.h>
#include <utility>
#include <unistd.h>

namespace live_stream {
namespace {

const char* kModuleName = "upgrade";
constexpr const char* kUpgradeUploadDir = "/tmp/live_stream/upgrade/uploads";
constexpr const char* kPersistedUpgradeInfoPath = "/data/upgrade_status.json";

bool IsTerminalState(UpgradeState state) {
    return state == UpgradeState::kIdle || state == UpgradeState::kCompleted ||
           state == UpgradeState::kFailed || state == UpgradeState::kCanceled;
}

bool IsCancelableState(UpgradeState state) {
    return state == UpgradeState::kValidating ||
           state == UpgradeState::kPreparing;
}

OperationResult ToOperationResult(bool ok) {
    return ok ? OperationResult::kSuccess : OperationResult::kFailed;
}

int64_t ElapsedMs(int64_t started_ms) {
    return infra::Time::MonotonicMillis() - started_ms;
}

UpgradeState UpgradeStateFromPersistedString(const std::string& state) {
    if (state == "validating") {
        return UpgradeState::kValidating;
    }
    if (state == "preparing") {
        return UpgradeState::kPreparing;
    }
    if (state == "writing") {
        return UpgradeState::kWriting;
    }
    if (state == "committing") {
        return UpgradeState::kCommitting;
    }
    if (state == "waiting_reboot") {
        return UpgradeState::kWaitingReboot;
    }
    if (state == "completed") {
        return UpgradeState::kCompleted;
    }
    if (state == "failed") {
        return UpgradeState::kFailed;
    }
    if (state == "canceled") {
        return UpgradeState::kCanceled;
    }
    return UpgradeState::kIdle;
}

UpgradeInfo LoadPersistedUpgradeInfo() {
    UpgradeInfo info;
    const std::string content =
        infra::File::ReadAll(kPersistedUpgradeInfoPath);
    if (content.empty()) {
        return info;
    }
    Json root = Json::parse(content, nullptr, false);
    if (!root.is_object()) {
        return info;
    }
    const std::string state =
        root.value("state", std::string("idle"));
    if (state == "completed") {
        static_cast<void>(infra::File::Remove(kPersistedUpgradeInfoPath));
        info.state = UpgradeState::kIdle;
        info.current_stage = UpgradeStateToString(info.state);
        info.ok = true;
        return info;
    }
    info.state = UpgradeStateFromPersistedString(state);
    info.progress_percent =
        infra::Clamp<uint32_t>(root.value("progress_percent", 0U), 0U, 100U);
    info.current_stage = root.value("current_stage", state);
    info.target_version = root.value("version", std::string());
    info.ok = root.value("ok", true);
    info.error_message = root.value("error_message", std::string());
    if (info.state == UpgradeState::kIdle) {
        info.current_stage = UpgradeStateToString(info.state);
    }
    return info;
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

    bool IsExternalFlashWriterActive() const override {
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

    std::string LastError() override {
        return "upgrade platform unavailable";
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

        upgrade_info_ = LoadPersistedUpgradeInfo();
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
        executor_options.workers = 1;
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
        upgrade_info_ = UpgradeInfo{};
        current_package_path_.clear();
        cancel_requested_ = false;
        external_flash_writer_active_ = false;
        initialized_ = false;
    }

public:
    UpgradeInfo GetUpgradeInfo() override {
        bool should_load_persisted_info = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            should_load_persisted_info = external_flash_writer_active_;
        }
        if (should_load_persisted_info) {
            const UpgradeInfo persisted_info = LoadPersistedUpgradeInfo();
            if (persisted_info.state != UpgradeState::kIdle) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (external_flash_writer_active_) {
                    const bool same_version =
                        upgrade_info_.target_version.empty() ||
                        persisted_info.target_version.empty() ||
                        upgrade_info_.target_version ==
                            persisted_info.target_version;
                    if (same_version) {
                        upgrade_info_.state = persisted_info.state;
                        upgrade_info_.progress_percent =
                            persisted_info.progress_percent;
                        upgrade_info_.current_stage =
                            persisted_info.current_stage;
                        upgrade_info_.target_version =
                            persisted_info.target_version;
                        upgrade_info_.ok = persisted_info.ok;
                        upgrade_info_.error_message =
                            persisted_info.error_message;
                        if (IsTerminalState(persisted_info.state)) {
                            upgrade_info_.finished_at_ms =
                                infra::Time::SystemTimeMillis();
                            external_flash_writer_active_ = false;
                            current_package_path_.clear();
                        }
                    }
                }
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return upgrade_info_;
    }

    UpgradePackageInfo ValidatePackage(
        const std::string& package_path) override {
        const int64_t started_at_ms = infra::Time::SystemTimeMillis();
        Info("upgrade", "service validate requested path=%s",
             package_path.c_str());
        std::string msg;
        std::string checked_package_path;
        if (!ValidateLocalPackage(package_path, &checked_package_path,
                                  &msg)) {
            SetLastError(msg);
            Warn("upgrade",
                 "service validate rejected path=%s reason=%s elapsed_ms=%lld",
                 package_path.c_str(), msg.c_str(),
                 static_cast<long long>(infra::Time::SystemTimeMillis() -
                                        started_at_ms));
            return UpgradePackageInfo();
        }
        IUpgradePlatform* platform = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_) {
                last_error_ = "service not initialized";
                Warn("upgrade",
                     "service validate rejected path=%s reason=%s elapsed_ms=%lld",
                     package_path.c_str(), last_error_.c_str(),
                     static_cast<long long>(infra::Time::SystemTimeMillis() -
                                            started_at_ms));
                return UpgradePackageInfo();
            }
            platform = platform_;
        }
        const int64_t validate_started_ms = infra::Time::MonotonicMillis();
        Info(kModuleName, "Upgrade package validation started path=%s",
             checked_package_path.c_str());
        UpgradePackageInfo info = platform->ValidatePackage(checked_package_path);
        if (info.version.empty()) {
            const std::string msg =
                PlatformErrorMsg(platform, "package validation failed");
            Warn(kModuleName,
                 "Upgrade package validation failed elapsed_ms=%lld msg=%s",
                 static_cast<long long>(ElapsedMs(validate_started_ms)),
                 msg.c_str());
            SetLastError(msg);
            Warn("upgrade",
                 "service validate failed path=%s checked_path=%s error=%s "
                 "elapsed_ms=%lld",
                 package_path.c_str(), checked_package_path.c_str(),
                 msg.c_str(),
                 static_cast<long long>(infra::Time::SystemTimeMillis() -
                                        started_at_ms));
        } else {
            Info(kModuleName,
                 "Upgrade package validation completed version=%s elapsed_ms=%lld",
                 info.version.c_str(),
                 static_cast<long long>(ElapsedMs(validate_started_ms)));
            SetLastError("");
            Info("upgrade",
                 "service validate ok path=%s checked_path=%s version=%s "
                 "size=%llu reboot=%d elapsed_ms=%lld",
                 package_path.c_str(), checked_package_path.c_str(),
                 info.version.c_str(),
                 static_cast<unsigned long long>(info.size_bytes),
                 info.requires_reboot ? 1 : 0,
                 static_cast<long long>(infra::Time::SystemTimeMillis() -
                                        started_at_ms));
        }
        return info;
    }

    std::string LastError() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_error_;
    }

    bool StartUpgrade(const live_stream::RequestContext& context,
                      const UpgradeRequest& request) override {
        const int64_t started_at_ms = infra::Time::SystemTimeMillis();
        Info("upgrade",
             "service start request request_id=%s user=%s path=%s "
             "expected_version=%s same=%d downgrade=%d auto_reboot=%d",
             context.request_id.c_str(), context.user_name.c_str(),
             request.package_path.c_str(), request.expected_version.c_str(),
             request.allow_same_version ? 1 : 0,
             request.allow_downgrade ? 1 : 0,
             request.auto_reboot ? 1 : 0);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_ || !executor_) {
                last_error_ = "service not started";
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "service not started");
                Warn("upgrade",
                     "service start rejected request_id=%s reason=service_not_started "
                     "elapsed_ms=%lld",
                     context.request_id.c_str(),
                     static_cast<long long>(infra::Time::SystemTimeMillis() -
                                            started_at_ms));
                return false;
            }
            if (!IsTerminalState(upgrade_info_.state)) {
                last_error_ = "upgrade busy";
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "upgrade busy");
                Warn("upgrade",
                     "service start rejected request_id=%s reason=busy state=%s "
                     "elapsed_ms=%lld",
                     context.request_id.c_str(),
                     UpgradeStateToString(upgrade_info_.state),
                     static_cast<long long>(infra::Time::SystemTimeMillis() -
                                            started_at_ms));
                return false;
            }
        }

        std::string msg;
        std::string checked_package_path;
        if (!ValidateLocalPackage(request.package_path, &checked_package_path,
                                  &msg)) {
            SetLastError(msg);
            RecordAudit(context, request.package_path, OperationResult::kRejected,
                        msg);
            Warn("upgrade",
                 "service start rejected request_id=%s path=%s reason=%s "
                 "elapsed_ms=%lld",
                 context.request_id.c_str(), request.package_path.c_str(),
                 msg.c_str(),
                 static_cast<long long>(infra::Time::SystemTimeMillis() -
                                        started_at_ms));
            return false;
        }

        event::Executor* executor = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_ || !executor_) {
                last_error_ = "service not started";
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "service not started");
                Warn("upgrade",
                     "service start rejected request_id=%s reason=service_not_started "
                     "elapsed_ms=%lld",
                     context.request_id.c_str(),
                     static_cast<long long>(infra::Time::SystemTimeMillis() -
                                            started_at_ms));
                return false;
            }
            if (!IsTerminalState(upgrade_info_.state)) {
                last_error_ = "upgrade busy";
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "upgrade busy");
                Warn("upgrade",
                     "service start rejected request_id=%s reason=busy state=%s "
                     "elapsed_ms=%lld",
                     context.request_id.c_str(),
                     UpgradeStateToString(upgrade_info_.state),
                     static_cast<long long>(infra::Time::SystemTimeMillis() -
                                            started_at_ms));
                return false;
            }
            cancel_requested_ = false;
            external_flash_writer_active_ = false;
            upgrade_info_ = UpgradeInfo{};
            current_package_path_ = checked_package_path;
            upgrade_info_.state = UpgradeState::kValidating;
            upgrade_info_.progress_percent = 0;
            upgrade_info_.current_stage = UpgradeStateToString(UpgradeState::kValidating);
            upgrade_info_.ok = true;
            upgrade_info_.started_at_ms = infra::Time::SystemTimeMillis();
            executor = executor_.get();
        }
        PublishProgressChanged();

        UpgradeRequest checked_request = request;
        checked_request.package_path = checked_package_path;
        if (executor->Post([this, context, checked_request]() {
                ExecuteUpgrade(context, checked_request);
            }) != event::EventStatus::kOk) {
            SetLastError("failed to queue upgrade task");
            SetFailed("failed to queue upgrade task", false);
            RecordAudit(context, request.package_path, OperationResult::kRejected,
                        "failed to queue upgrade task");
            Warn("upgrade",
                 "service start queue failed request_id=%s path=%s "
                 "elapsed_ms=%lld",
                 context.request_id.c_str(), checked_package_path.c_str(),
                 static_cast<long long>(infra::Time::SystemTimeMillis() -
                                        started_at_ms));
            return false;
        }
        Info("upgrade",
             "service start queued request_id=%s path=%s elapsed_ms=%lld",
             context.request_id.c_str(), checked_package_path.c_str(),
             static_cast<long long>(infra::Time::SystemTimeMillis() -
                                    started_at_ms));
        return true;
    }

    bool CancelUpgrade(const live_stream::RequestContext& context) override {
        IUpgradePlatform* platform = nullptr;
        std::string target;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_) {
                RecordAudit(context, "upgrade", OperationResult::kRejected,
                            "service not started");
                return false;
            }
            if (!IsCancelableState(upgrade_info_.state)) {
                RecordAudit(context, upgrade_info_.target_version,
                            OperationResult::kRejected, "upgrade not cancelable");
                return false;
            }
            cancel_requested_ = true;
            target = upgrade_info_.target_version;
            platform = platform_;
        }
        const bool cancel_ok = platform->CancelUpgrade();
        if (!cancel_ok) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                cancel_requested_ = false;
            }
            PublishProgressChanged();
            const std::string msg = PlatformErrorMsg(platform, "cancel failed");
            RecordAudit(context, target, OperationResult::kFailed, msg);
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            upgrade_info_.state = UpgradeState::kCanceled;
            upgrade_info_.current_stage = UpgradeStateToString(UpgradeState::kCanceled);
            upgrade_info_.ok = true;
            upgrade_info_.error_message = "canceled";
            upgrade_info_.finished_at_ms = infra::Time::SystemTimeMillis();
        }
        CleanupCurrentPackageFile();
        PublishProgressChanged();
        RecordAudit(context, target, OperationResult::kSuccess, "canceled");
        return true;
    }

    bool ConfirmReboot(const live_stream::RequestContext& context) override {
        IUpgradePlatform* platform = nullptr;
        std::string target;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_) {
                RecordAudit(context, "upgrade", OperationResult::kRejected,
                            "service not started");
                return false;
            }
            if (upgrade_info_.state != UpgradeState::kWaitingReboot) {
                RecordAudit(context, upgrade_info_.target_version,
                            OperationResult::kRejected, "reboot not pending");
                return false;
            }
            platform = platform_;
            target = upgrade_info_.target_version;
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
                              std::string* msg) {
        if (checked_package_path == nullptr) {
            return false;
        }
        checked_package_path->clear();
        if (package_path.empty() ||
            package_path.size() > options_.max_package_path_length) {
            if (msg != nullptr) {
                *msg = "invalid package path";
            }
            return false;
        }
        struct stat link_stat;
        if (lstat(package_path.c_str(), &link_stat) != 0 ||
            S_ISLNK(link_stat.st_mode)) {
            if (msg != nullptr) {
                *msg = "package path is not allowed";
            }
            return false;
        }
        char resolved_path[PATH_MAX] = {0};
        if (realpath(package_path.c_str(), resolved_path) == nullptr) {
            if (msg != nullptr) {
                *msg = "package path is not allowed";
            }
            return false;
        }
        const std::string resolved_package_path(resolved_path);
        const std::string upload_prefix =
            std::string(kUpgradeUploadDir) + "/";
        if (resolved_package_path.compare(0, upload_prefix.size(),
                                          upload_prefix) != 0) {
            if (msg != nullptr) {
                *msg = "package path is outside upload directory";
            }
            return false;
        }

        struct stat file_stat;
        if (stat(resolved_package_path.c_str(), &file_stat) != 0) {
            if (msg != nullptr) {
                *msg = "package not found";
            }
            return false;
        }
        if (!S_ISREG(file_stat.st_mode)) {
            if (msg != nullptr) {
                *msg = "package path is not a regular file";
            }
            return false;
        }
        if (file_stat.st_size <= 0) {
            if (msg != nullptr) {
                *msg = "package is empty";
            }
            return false;
        }
        if (static_cast<uint64_t>(file_stat.st_size) >
            options_.max_package_size_bytes) {
            if (msg != nullptr) {
                *msg = "package too large";
            }
            return false;
        }
        *checked_package_path = resolved_package_path;
        if (msg != nullptr) {
            msg->clear();
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
            Error(kModuleName, "Upgrade task failed msg=platform_missing");
            SetFailed("platform missing", false);
            RecordAudit(context, request.package_path, OperationResult::kFailed,
                        "platform missing");
            return;
        }

        const int64_t validate_started_ms = infra::Time::MonotonicMillis();
        Info(kModuleName, "Upgrade task validate started package=%s",
             request.package_path.c_str());
        UpgradePackageInfo info =
            platform->ValidatePackage(request.package_path);
        if (info.version.empty()) {
            if (IsCancelRequested()) {
                return;
            }
            const std::string msg =
                PlatformErrorMsg(platform, "package validation failed");
            Warn(kModuleName,
                 "Upgrade task validate failed elapsed_ms=%lld msg=%s",
                 static_cast<long long>(ElapsedMs(validate_started_ms)),
                 msg.c_str());
            CleanupCurrentPackageFile();
            SetFailed(msg, false);
            RecordAudit(context, request.package_path, OperationResult::kFailed,
                        msg);
            return;
        }
        Info(kModuleName,
             "Upgrade task validate completed version=%s elapsed_ms=%lld",
             info.version.c_str(),
             static_cast<long long>(ElapsedMs(validate_started_ms)));
        if (info.package_path.empty()) {
            info.package_path = request.package_path;
        }

        std::string policy_msg;
        if (!ValidateVersionPolicy(platform, info, request, &policy_msg)) {
            if (IsCancelRequested()) {
                return;
            }
            Warn(kModuleName, "Upgrade task policy rejected version=%s msg=%s",
                 info.version.c_str(), policy_msg.c_str());
            CleanupCurrentPackageFile();
            SetFailed(policy_msg, false);
            RecordAudit(context, info.version, OperationResult::kRejected,
                        policy_msg);
            return;
        }

        if (IsCancelRequested()) {
            return;
        }
        UpdateTargetVersion(info.version);
        UpdateStatus(UpgradeState::kPreparing, 10, true, "");
        const int64_t prepare_started_ms = infra::Time::MonotonicMillis();
        Info(kModuleName, "Upgrade task prepare started version=%s",
             info.version.c_str());
        if (!platform->PrepareUpgrade(info)) {
            if (IsCancelRequested()) {
                return;
            }
            const std::string msg =
                PlatformErrorMsg(platform, "prepare upgrade failed");
            Warn(kModuleName,
                 "Upgrade task prepare failed elapsed_ms=%lld msg=%s",
                 static_cast<long long>(ElapsedMs(prepare_started_ms)),
                 msg.c_str());
            static_cast<void>(platform->CleanupFailedUpgrade());
            CleanupCurrentPackageFile();
            SetFailed(msg, false);
            RecordAudit(context, info.version, OperationResult::kFailed,
                        msg);
            return;
        }
        Info(kModuleName, "Upgrade task prepare completed elapsed_ms=%lld",
             static_cast<long long>(ElapsedMs(prepare_started_ms)));

        if (IsCancelRequested()) {
            return;
        }
        UpdateStatus(UpgradeState::kWriting, 20, true, "");
        const int64_t write_started_ms = infra::Time::MonotonicMillis();
        Info(kModuleName, "Upgrade task write started version=%s",
             info.version.c_str());
        platform->SetAutoRebootPolicy(request.auto_reboot);
        const bool write_ok = platform->WriteUpgrade(
            request.package_path, [this](uint32_t progress_percent) {
                const uint32_t bounded =
                    infra::Clamp<uint32_t>(progress_percent, 0U, 100U);
                const uint32_t service_progress = 20U + (bounded * 60U) / 100U;
                UpdateWritingProgress(service_progress);
            });
        if (!write_ok) {
            if (!IsCancelRequested()) {
                const std::string msg =
                    PlatformErrorMsg(platform, "write upgrade failed");
                Warn(kModuleName,
                     "Upgrade task write failed elapsed_ms=%lld msg=%s",
                     static_cast<long long>(ElapsedMs(write_started_ms)),
                     msg.c_str());
                static_cast<void>(platform->CleanupFailedUpgrade());
                CleanupCurrentPackageFile();
                SetFailed(msg, false);
                RecordAudit(context, info.version, OperationResult::kFailed,
                            msg);
            }
            return;
        }
        Info(kModuleName, "Upgrade task write completed elapsed_ms=%lld",
             static_cast<long long>(ElapsedMs(write_started_ms)));

        if (platform->IsExternalFlashWriterActive()) {
            MarkExternalFlashWriterActive(
                "sysupgrade helper is writing flash; main service will stop");
            Info(kModuleName,
                 "Upgrade task handed to external flash writer version=%s",
                 info.version.c_str());
            RecordAudit(context, info.version, OperationResult::kSuccess,
                        "sysupgrade helper handoff");
            return;
        }

        if (IsCancelRequested()) {
            return;
        }
        UpdateStatus(UpgradeState::kCommitting, 90, true, "");
        const int64_t commit_started_ms = infra::Time::MonotonicMillis();
        Info(kModuleName, "Upgrade task commit started version=%s",
             info.version.c_str());
        if (!platform->CommitUpgrade(info)) {
            const std::string msg =
                PlatformErrorMsg(platform, "commit upgrade failed");
            Warn(kModuleName,
                 "Upgrade task commit failed elapsed_ms=%lld msg=%s",
                 static_cast<long long>(ElapsedMs(commit_started_ms)),
                 msg.c_str());
            CleanupCurrentPackageFile();
            SetFailed(msg, true);
            RecordAudit(context, info.version, OperationResult::kFailed,
                        msg);
            return;
        }
        Info(kModuleName, "Upgrade task commit completed elapsed_ms=%lld",
             static_cast<long long>(ElapsedMs(commit_started_ms)));
        CleanupCurrentPackageFile();

        if (info.requires_reboot) {
            if (request.auto_reboot) {
                const int64_t reboot_started_ms =
                    infra::Time::MonotonicMillis();
                Info(kModuleName, "Upgrade task reboot started version=%s",
                     info.version.c_str());
                if (!platform->RebootToApply()) {
                    const std::string msg =
                        PlatformErrorMsg(platform, "reboot failed");
                    Warn(kModuleName,
                         "Upgrade task reboot failed elapsed_ms=%lld msg=%s",
                         static_cast<long long>(ElapsedMs(reboot_started_ms)),
                         msg.c_str());
                    SetFailed(msg, true);
                    RecordAudit(context, info.version, OperationResult::kFailed,
                                msg);
                    return;
                }
                Info(kModuleName,
                     "Upgrade task reboot completed elapsed_ms=%lld",
                     static_cast<long long>(ElapsedMs(reboot_started_ms)));
                UpdateStatus(UpgradeState::kCompleted, 100, true, "");
                RecordAudit(context, info.version, OperationResult::kSuccess,
                            "completed");
                return;
            }
            UpdateStatus(UpgradeState::kWaitingReboot, 100, true, "");
            Info(kModuleName, "Upgrade task waiting_reboot version=%s",
                 info.version.c_str());
            RecordAudit(context, info.version, OperationResult::kSuccess,
                        "waiting reboot");
            return;
        }

        UpdateStatus(UpgradeState::kCompleted, 100, true, "");
        Info(kModuleName, "Upgrade task completed version=%s",
             info.version.c_str());
        RecordAudit(context, info.version, OperationResult::kSuccess, "completed");
    }

    bool ValidateVersionPolicy(IUpgradePlatform* platform,
                               const UpgradePackageInfo& info,
                               const UpgradeRequest& request,
                               std::string* msg) {
        if (!request.expected_version.empty() &&
            request.expected_version != info.version) {
            if (msg != nullptr) {
                *msg = "unexpected package version";
            }
            return false;
        }

        const std::string current_version = platform->GetCurrentVersion();
        if (current_version.empty()) {
            if (msg != nullptr) {
                *msg = "current version unavailable";
            }
            return false;
        }
        const int compare = platform->CompareVersion(info.version, current_version);
        if (compare == 0 && !request.allow_same_version) {
            if (msg != nullptr) {
                *msg = "same version is not allowed";
            }
            return false;
        }
        if (compare < 0 && !request.allow_downgrade) {
            if (msg != nullptr) {
                *msg = "downgrade is not allowed";
            }
            return false;
        }
        if (msg != nullptr) {
            msg->clear();
        }
        return true;
    }

    std::string PlatformErrorMsg(IUpgradePlatform* platform,
                                 const std::string& fallback) {
        if (platform == nullptr) {
            return fallback;
        }
        const std::string msg = platform->LastError();
        return msg.empty() ? fallback : msg;
    }

    void CleanupPackageFile(const std::string& package_path) {
        if (package_path.empty()) {
            return;
        }
        static_cast<void>(std::remove(package_path.c_str()));
    }

    void CleanupCurrentPackageFile() {
        std::string package_path;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            package_path = current_package_path_;
            current_package_path_.clear();
        }
        CleanupPackageFile(package_path);
    }

    void SetLastError(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = msg;
    }

    bool IsCancelRequested() {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancel_requested_;
    }

    void UpdateTargetVersion(const std::string& version) {
        std::lock_guard<std::mutex> lock(mutex_);
        upgrade_info_.target_version = version;
    }

    void UpdateWritingProgress(uint32_t progress_percent) {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (upgrade_info_.state != UpgradeState::kWriting || cancel_requested_) {
                return;
            }
            progress_percent =
                infra::Clamp<uint32_t>(progress_percent, 0U, 89U);
            if (progress_percent > upgrade_info_.progress_percent) {
                upgrade_info_.progress_percent = progress_percent;
                changed = true;
            }
        }
        if (changed) {
            PublishProgressChanged();
        }
    }

    void SetCurrentStage(const std::string& current_stage) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            upgrade_info_.current_stage = current_stage;
        }
        PublishProgressChanged();
    }

    void MarkExternalFlashWriterActive(const std::string& current_stage) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            external_flash_writer_active_ = true;
            upgrade_info_.state = UpgradeState::kWriting;
            if (upgrade_info_.progress_percent < 20U) {
                upgrade_info_.progress_percent = 20U;
            }
            upgrade_info_.current_stage = current_stage;
            upgrade_info_.ok = true;
            upgrade_info_.error_message.clear();
        }
        PublishProgressChanged();
    }

    void UpdateStatus(UpgradeState state,
                      uint32_t progress_percent,
                      bool ok,
                      const std::string& error_message) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            upgrade_info_.state = state;
            upgrade_info_.progress_percent =
                infra::Clamp<uint32_t>(progress_percent, 0U, 100U);
            upgrade_info_.current_stage = UpgradeStateToString(state);
            upgrade_info_.ok = ok;
            upgrade_info_.error_message = error_message;
            if (state == UpgradeState::kCompleted || state == UpgradeState::kFailed ||
                state == UpgradeState::kCanceled) {
                upgrade_info_.finished_at_ms = infra::Time::SystemTimeMillis();
            }
        }
        PublishProgressChanged();
    }

    void SetFailed(const std::string& msg,
                   bool committed) {
        std::string full_message = msg;
        if (committed && !full_message.empty()) {
            full_message += "; committed upgrade may require manual recovery";
        } else if (committed) {
            full_message = "committed upgrade may require manual recovery";
        }
        uint32_t failed_progress = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            failed_progress = upgrade_info_.progress_percent;
        }
        UpdateStatus(UpgradeState::kFailed, failed_progress, false, full_message);
    }

    void PublishProgressChanged() {
        event::EventCenter* event_center = options_.event;
        if (event_center == nullptr) {
            return;
        }
        UpgradeInfo upgrade_info = GetUpgradeInfo();
        event::Event progress_event;
        progress_event.type = event::EventType::kUpgradeProgressChanged;
        progress_event.source = kModuleName;
        progress_event.msg = upgrade_info.current_stage;
        progress_event.value =
            static_cast<int32_t>(upgrade_info.progress_percent);
        static_cast<void>(event_center->Publish(progress_event));
    }

    void RecordAudit(const live_stream::RequestContext& context,
                     const std::string& target,
                     OperationResult result,
                     const std::string& msg) {
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
        record.module = kModuleName;
        record.action = OperationAction::kUpgrade;
        record.target = target;
        record.result = result;
        record.reason = msg;
        static_cast<void>(logger->RecordOperation(record));
    }

    UpgradeOptions options_;
    std::unique_ptr<event::Executor> executor_;
    std::unique_ptr<IUpgradePlatform> restricted_platform_;
    IUpgradePlatform* platform_ = nullptr;
    mutable std::mutex mutex_;
    UpgradeInfo upgrade_info_;
    std::string current_package_path_;
    std::string last_error_;
    bool initialized_ = false;
    bool started_ = false;
    bool cancel_requested_ = false;
    bool external_flash_writer_active_ = false;
};

}  // namespace

const char* Upgrade::Name() {
    return kModuleName;
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
