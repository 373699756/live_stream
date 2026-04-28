#include "upgrade_service.h"

#include "event_service.h"
#include "infra/time.h"
#include "infra/executor.h"
#include "logger_service.h"

#include <algorithm>
#include <mutex>
#include <sys/stat.h>
#include <utility>

namespace live_stream {
namespace {

const char* kServiceName = "upgrade_service";

bool IsTerminalState(UpgradeState state) {
    return state == UpgradeState::kIdle || state == UpgradeState::kCompleted ||
           state == UpgradeState::kFailed || state == UpgradeState::kCanceled;
}

bool IsCancelableState(UpgradeState state) {
    return state == UpgradeState::kValidating ||
           state == UpgradeState::kPreparing ||
           state == UpgradeState::kWriting;
}

OperationResult ToOperationResult(infra::Status error) {
    return error == infra::Status::kOk ? OperationResult::kSuccess
                                      : OperationResult::kFailed;
}

class RestrictedUpgradePlatform : public IUpgradePlatform {
 public:
    infra::Result<UpgradePackageInfo> ValidatePackage(
        const std::string& package_path) override {
        (void)package_path;
        return infra::Result<UpgradePackageInfo>::Fail(
            infra::Status::kNotSupported);
    }

    infra::Result<std::string> GetCurrentVersion() override {
        return infra::Result<std::string>::Fail(infra::Status::kNotSupported);
    }

    infra::Result<int> CompareVersion(const std::string& lhs,
                                      const std::string& rhs) override {
        (void)lhs;
        (void)rhs;
        return infra::Result<int>::Fail(infra::Status::kNotSupported);
    }

    infra::Status PrepareUpgrade(const UpgradePackageInfo& info) override {
        (void)info;
        return infra::Status::kNotSupported;
    }

    infra::Status WriteUpgrade(
        const std::string& package_path,
        UpgradeProgressCallback progress_callback) override {
        (void)package_path;
        (void)progress_callback;
        return infra::Status::kNotSupported;
    }

    infra::Status CommitUpgrade(const UpgradePackageInfo& info) override {
        (void)info;
        return infra::Status::kNotSupported;
    }

    infra::Status CancelUpgrade() override {
        return infra::Status::kNotSupported;
    }

    infra::Status RebootToApply() override {
        return infra::Status::kNotSupported;
    }

    infra::Status CleanupFailedUpgrade() override {
        return infra::Status::kOk;
    }
};

class UpgradeServiceImpl : public IUpgradeService {
 public:
    explicit UpgradeServiceImpl(UpgradeServiceOptions options)
        : options_(std::move(options)) {}

    ~UpgradeServiceImpl() override {
        Stop();
        Deinit();
    }

    infra::Status Init() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return infra::Status::kOk;
        }
        if (options_.max_package_size_bytes == 0 ||
            options_.max_package_path_length == 0 ||
            options_.queue_capacity == 0) {
            return infra::Status::kInvalidParam;
        }
        if (options_.platform == nullptr) {
            restricted_platform_.reset(new RestrictedUpgradePlatform());
            platform_ = restricted_platform_.get();
        } else {
            platform_ = options_.platform;
        }

        executor_.reset(new infra::Executor());

        status_ = UpgradeStatus{};
        initialized_ = true;
        return infra::Status::kOk;
    }

    infra::Status Start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !executor_) {
            return infra::Status::kBusy;
        }
        if (started_) {
            return infra::Status::kOk;
        }
        infra::ExecutorOptions executor_options;
        executor_options.worker_count = 1;
        executor_options.queue_capacity = options_.queue_capacity;
        const infra::Status error = executor_->Start(executor_options);
        if (error != infra::Status::kOk) {
            return error;
        }
        started_ = true;
        return infra::Status::kOk;
    }

    void Stop() override {
        std::unique_ptr<infra::Executor> executor;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = false;
            executor = std::move(executor_);
        }
        if (executor) {
            executor->Stop(infra::StopMode::kDiscard);
            std::lock_guard<std::mutex> lock(mutex_);
            if (!executor_) {
                executor_ = std::move(executor);
            }
        }
    }

    void Deinit() override {
        Stop();
        std::lock_guard<std::mutex> lock(mutex_);
        executor_.reset();
        restricted_platform_.reset();
        platform_ = nullptr;
        status_ = UpgradeStatus{};
        cancel_requested_ = false;
        initialized_ = false;
    }

    const char* Name() const override { return kServiceName; }

    UpgradeStatus GetStatus() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

    infra::Result<UpgradePackageInfo> ValidatePackage(
        const std::string& package_path) override {
        const infra::Status local_error = ValidateLocalPackage(package_path);
        if (local_error != infra::Status::kOk) {
            return infra::Result<UpgradePackageInfo>::Fail(local_error);
        }
        IUpgradePlatform* platform = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || platform_ == nullptr) {
                return infra::Result<UpgradePackageInfo>::Fail(
                    infra::Status::kBusy);
            }
            platform = platform_;
        }
        return platform->ValidatePackage(package_path);
    }

    infra::Status StartUpgrade(const infra::RequestContext& context,
                              const UpgradeRequest& request) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_ || !executor_) {
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "service not started");
                return infra::Status::kBusy;
            }
            if (!IsTerminalState(status_.state)) {
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "upgrade busy");
                return infra::Status::kBusy;
            }
        }

        const infra::Status local_error = ValidateLocalPackage(request.package_path);
        if (local_error != infra::Status::kOk) {
            RecordAudit(context, request.package_path, OperationResult::kRejected,
                        infra::StatusToString(local_error));
            return local_error;
        }

        infra::Executor* executor = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_ || !executor_) {
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "service not started");
                return infra::Status::kBusy;
            }
            if (!IsTerminalState(status_.state)) {
                RecordAudit(context, request.package_path,
                            OperationResult::kRejected, "upgrade busy");
                return infra::Status::kBusy;
            }
            cancel_requested_ = false;
            status_ = UpgradeStatus{};
            status_.state = UpgradeState::kValidating;
            status_.progress_percent = 0;
            status_.current_stage = UpgradeStateToString(UpgradeState::kValidating);
            status_.status = infra::Status::kOk;
            status_.started_at_ms = infra::Time::SystemTimeMillis();
            executor = executor_.get();
        }
        PublishProgressChanged();

        const infra::Status post_error = executor->Post(
            [this, context, request]() { ExecuteUpgrade(context, request); });
        if (post_error != infra::Status::kOk) {
            SetFailed(post_error, "failed to queue upgrade task", false);
            RecordAudit(context, request.package_path, OperationResult::kRejected,
                        infra::StatusToString(post_error));
            return post_error;
        }
        return infra::Status::kOk;
    }

    infra::Status CancelUpgrade(const infra::RequestContext& context) override {
        IUpgradePlatform* platform = nullptr;
        std::string target;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_ || platform_ == nullptr) {
                RecordAudit(context, "upgrade", OperationResult::kRejected,
                            "service not started");
                return infra::Status::kBusy;
            }
            if (!IsCancelableState(status_.state)) {
                RecordAudit(context, status_.target_version,
                            OperationResult::kRejected, "upgrade not cancelable");
                return infra::Status::kBusy;
            }
            cancel_requested_ = true;
            target = status_.target_version;
            status_.state = UpgradeState::kCanceled;
            status_.current_stage = UpgradeStateToString(UpgradeState::kCanceled);
            status_.status = infra::Status::kOk;
            status_.error_message = "canceled";
            status_.finished_at_ms = infra::Time::SystemTimeMillis();
            platform = platform_;
        }
        const infra::Status cancel_error = platform->CancelUpgrade();
        PublishProgressChanged();
        RecordAudit(context, target, ToOperationResult(cancel_error), "canceled");
        return cancel_error;
    }

    infra::Status ConfirmReboot(const infra::RequestContext& context) override {
        IUpgradePlatform* platform = nullptr;
        std::string target;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || !started_ || platform_ == nullptr) {
                RecordAudit(context, "upgrade", OperationResult::kRejected,
                            "service not started");
                return infra::Status::kBusy;
            }
            if (status_.state != UpgradeState::kWaitingReboot) {
                RecordAudit(context, status_.target_version,
                            OperationResult::kRejected, "reboot not pending");
                return infra::Status::kBusy;
            }
            platform = platform_;
            target = status_.target_version;
        }

        const infra::Status error = platform->RebootToApply();
        if (error == infra::Status::kOk) {
            UpdateStatus(UpgradeState::kCompleted, 100, infra::Status::kOk, "");
        } else {
            SetFailed(error, infra::StatusToString(error), true);
        }
        RecordAudit(context, target, ToOperationResult(error),
                    infra::StatusToString(error));
        return error;
    }

 private:
    infra::Status ValidateLocalPackage(const std::string& package_path) {
        if (package_path.empty() ||
            package_path.size() > options_.max_package_path_length) {
            return infra::Status::kInvalidParam;
        }

        struct stat file_stat;
        if (stat(package_path.c_str(), &file_stat) != 0) {
            return infra::Status::kNotFound;
        }
        if (!S_ISREG(file_stat.st_mode)) {
            return infra::Status::kInvalidParam;
        }
        if (file_stat.st_size <= 0) {
            return infra::Status::kInvalidParam;
        }
        if (static_cast<uint64_t>(file_stat.st_size) >
            options_.max_package_size_bytes) {
            return infra::Status::kInvalidParam;
        }
        return infra::Status::kOk;
    }

    void ExecuteUpgrade(const infra::RequestContext& context,
                        const UpgradeRequest& request) {
        IUpgradePlatform* platform = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            platform = platform_;
        }
        if (platform == nullptr) {
            SetFailed(infra::Status::kInternalError, "platform missing", false);
            RecordAudit(context, request.package_path, OperationResult::kFailed,
                        "platform missing");
            return;
        }

        infra::Result<UpgradePackageInfo> package_result =
            platform->ValidatePackage(request.package_path);
        if (!package_result.IsOk()) {
            SetFailed(package_result.status, infra::StatusToString(package_result.status),
                      false);
            RecordAudit(context, request.package_path, OperationResult::kFailed,
                        infra::StatusToString(package_result.status));
            return;
        }
        UpgradePackageInfo info = package_result.value;
        if (info.package_path.empty()) {
            info.package_path = request.package_path;
        }

        const infra::Status policy_error = ValidateVersionPolicy(
            platform, info, request);
        if (policy_error != infra::Status::kOk) {
            SetFailed(policy_error, infra::StatusToString(policy_error), false);
            RecordAudit(context, info.version, OperationResult::kRejected,
                        infra::StatusToString(policy_error));
            return;
        }

        if (IsCancelRequested()) {
            return;
        }
        UpdateTargetVersion(info.version);
        UpdateStatus(UpgradeState::kPreparing, 10, infra::Status::kOk, "");
        const infra::Status prepare_error = platform->PrepareUpgrade(info);
        if (prepare_error != infra::Status::kOk) {
            static_cast<void>(platform->CleanupFailedUpgrade());
            SetFailed(prepare_error, infra::StatusToString(prepare_error), false);
            RecordAudit(context, info.version, OperationResult::kFailed,
                        infra::StatusToString(prepare_error));
            return;
        }

        if (IsCancelRequested()) {
            return;
        }
        UpdateStatus(UpgradeState::kWriting, 20, infra::Status::kOk, "");
        const infra::Status write_error = platform->WriteUpgrade(
            request.package_path, [this](uint32_t progress_percent) {
                const uint32_t bounded = std::min(progress_percent, 100U);
                const uint32_t service_progress = 20U + (bounded * 60U) / 100U;
                UpdateWritingProgress(service_progress);
            });
        if (write_error != infra::Status::kOk) {
            if (!IsCancelRequested()) {
                static_cast<void>(platform->CleanupFailedUpgrade());
                SetFailed(write_error, infra::StatusToString(write_error), false);
                RecordAudit(context, info.version, OperationResult::kFailed,
                            infra::StatusToString(write_error));
            }
            return;
        }

        if (IsCancelRequested()) {
            return;
        }
        UpdateStatus(UpgradeState::kCommitting, 90, infra::Status::kOk, "");
        const infra::Status commit_error = platform->CommitUpgrade(info);
        if (commit_error != infra::Status::kOk) {
            SetFailed(commit_error, infra::StatusToString(commit_error), true);
            RecordAudit(context, info.version, OperationResult::kFailed,
                        infra::StatusToString(commit_error));
            return;
        }

        if (info.requires_reboot) {
            if (request.auto_reboot) {
                const infra::Status reboot_error = platform->RebootToApply();
                if (reboot_error != infra::Status::kOk) {
                    SetFailed(reboot_error, infra::StatusToString(reboot_error), true);
                    RecordAudit(context, info.version, OperationResult::kFailed,
                                infra::StatusToString(reboot_error));
                    return;
                }
                UpdateStatus(UpgradeState::kCompleted, 100, infra::Status::kOk, "");
                RecordAudit(context, info.version, OperationResult::kSuccess,
                            "completed");
                return;
            }
            UpdateStatus(UpgradeState::kWaitingReboot, 100, infra::Status::kOk, "");
            RecordAudit(context, info.version, OperationResult::kSuccess,
                        "waiting reboot");
            return;
        }

        UpdateStatus(UpgradeState::kCompleted, 100, infra::Status::kOk, "");
        RecordAudit(context, info.version, OperationResult::kSuccess, "completed");
    }

    infra::Status ValidateVersionPolicy(IUpgradePlatform* platform,
                                       const UpgradePackageInfo& info,
                                       const UpgradeRequest& request) {
        if (!request.expected_version.empty() &&
            request.expected_version != info.version) {
            return infra::Status::kInvalidParam;
        }

        infra::Result<std::string> current_result = platform->GetCurrentVersion();
        if (!current_result.IsOk()) {
            return current_result.status;
        }
        infra::Result<int> compare_result =
            platform->CompareVersion(info.version, current_result.value);
        if (!compare_result.IsOk()) {
            return compare_result.status;
        }
        if (compare_result.value == 0 && !request.allow_same_version) {
            return infra::Status::kAlreadyExists;
        }
        if (compare_result.value < 0 && !request.allow_downgrade) {
            return infra::Status::kInvalidParam;
        }
        return infra::Status::kOk;
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
            progress_percent = std::min(progress_percent, 89U);
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
                      infra::Status error,
                      const std::string& error_message) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.state = state;
            status_.progress_percent = std::min(progress_percent, 100U);
            status_.current_stage = UpgradeStateToString(state);
            status_.status = error;
            status_.error_message = error_message;
            if (state == UpgradeState::kCompleted || state == UpgradeState::kFailed ||
                state == UpgradeState::kCanceled) {
                status_.finished_at_ms = infra::Time::SystemTimeMillis();
            }
        }
        PublishProgressChanged();
    }

    void SetFailed(infra::Status error,
                   const std::string& message,
                   bool committed) {
        std::string full_message = message;
        if (committed && !full_message.empty()) {
            full_message += "; committed upgrade may require manual recovery";
        } else if (committed) {
            full_message = "committed upgrade may require manual recovery";
        }
        UpdateStatus(UpgradeState::kFailed, 100, error, full_message);
    }

    void PublishProgressChanged() {
        IEventService* event_service = options_.event_service;
        if (event_service == nullptr) {
            return;
        }
        UpgradeStatus status = GetStatus();
        Event event;
        event.type = EventType::kUpgradeProgressChanged;
        event.source = kServiceName;
        event.message = status.current_stage;
        event.value = static_cast<int32_t>(status.progress_percent);
        static_cast<void>(event_service->Publish(event));
    }

    void RecordAudit(const infra::RequestContext& context,
                     const std::string& target,
                     OperationResult result,
                     const std::string& reason) {
        ILoggerService* logger_service = options_.logger_service;
        if (logger_service == nullptr) {
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
        static_cast<void>(logger_service->RecordOperation(record));
    }

    UpgradeServiceOptions options_;
    std::unique_ptr<infra::Executor> executor_;
    std::unique_ptr<IUpgradePlatform> restricted_platform_;
    IUpgradePlatform* platform_ = nullptr;
    std::mutex mutex_;
    UpgradeStatus status_;
    bool initialized_ = false;
    bool started_ = false;
    bool cancel_requested_ = false;
};

}  // namespace

const char* UpgradeService::Name() {
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

std::unique_ptr<IUpgradeService> CreateUpgradeService(
    const UpgradeServiceOptions& options) {
    return std::unique_ptr<IUpgradeService>(new UpgradeServiceImpl(options));
}

}  // namespace live_stream
