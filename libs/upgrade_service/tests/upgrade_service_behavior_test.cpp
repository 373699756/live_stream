#include "upgrade_service.h"

#include "event_service.h"
#include "infra/time.h"
#include "logger_service.h"

#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using live_stream::IUpgradePlatform;
using live_stream::UpgradePackageInfo;
using live_stream::UpgradeProgressCallback;
using live_stream::UpgradeState;

std::string MakePackageFile(const char* name, std::size_t size) {
    std::string path = "/tmp/";
    path += name;
    path += "_";
    path += std::to_string(static_cast<long long>(getpid()));
    std::ofstream output(path, std::ios::binary);
    for (std::size_t i = 0; i < size; ++i) {
        output.put('x');
    }
    return path;
}

struct FakeEventService : live_stream::IEventService {
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake_event"; }

    infra::Result<live_stream::EventSubscriptionId> Subscribe(
        live_stream::EventType type,
        live_stream::EventHandler handler) override {
        (void)type;
        (void)handler;
        return infra::Result<live_stream::EventSubscriptionId>::Ok(1);
    }

    infra::Status Unsubscribe(
        live_stream::EventSubscriptionId subscription_id) override {
        (void)subscription_id;
        return infra::Status::kOk;
    }

    infra::Status Publish(const live_stream::Event& event) override {
        events.push_back(event);
        return infra::Status::kOk;
    }

    std::vector<live_stream::Event> events;
};

struct FakeLoggerService : live_stream::ILoggerService {
    infra::Status Init() override { return infra::Status::kOk; }
    infra::Status Start() override { return infra::Status::kOk; }
    void Stop() override {}
    void Deinit() override {}
    const char* Name() const override { return "fake_logger"; }

    infra::Status RecordOperation(
        const live_stream::OperationRecord& record) override {
        records.push_back(record);
        return infra::Status::kOk;
    }

    infra::Result<std::vector<live_stream::OperationRecord>> QueryOperations(
        const live_stream::OperationLogQuery& query) override {
        (void)query;
        return infra::Result<std::vector<live_stream::OperationRecord>>::Ok(
            records);
    }

    infra::Status ExportOperations(
        const live_stream::OperationLogExportOptions& options) override {
        (void)options;
        return infra::Status::kOk;
    }

    std::vector<live_stream::OperationRecord> records;
};

struct FakeUpgradePlatform : IUpgradePlatform {
    infra::Result<UpgradePackageInfo> ValidatePackage(
        const std::string& package_path) override {
        calls.push_back("validate");
        if (validate_error != infra::Status::kOk) {
            return infra::Result<UpgradePackageInfo>::Fail(validate_error);
        }
        UpgradePackageInfo result = info;
        result.package_path = package_path;
        result.size_bytes = 16;
        return infra::Result<UpgradePackageInfo>::Ok(result);
    }

    infra::Result<std::string> GetCurrentVersion() override {
        calls.push_back("current_version");
        if (current_version_error != infra::Status::kOk) {
            return infra::Result<std::string>::Fail(current_version_error);
        }
        return infra::Result<std::string>::Ok(current_version);
    }

    infra::Result<int> CompareVersion(const std::string& lhs,
                                      const std::string& rhs) override {
        calls.push_back("compare");
        if (compare_error != infra::Status::kOk) {
            return infra::Result<int>::Fail(compare_error);
        }
        int value = 0;
        if (lhs < rhs) {
            value = -1;
        } else if (lhs > rhs) {
            value = 1;
        }
        return infra::Result<int>::Ok(value);
    }

    infra::Status PrepareUpgrade(const UpgradePackageInfo& package_info) override {
        (void)package_info;
        calls.push_back("prepare");
        return prepare_error;
    }

    infra::Status WriteUpgrade(
        const std::string& package_path,
        UpgradeProgressCallback progress_callback) override {
        (void)package_path;
        calls.push_back("write");
        progress_callback(0);
        progress_callback(50);
        {
            std::unique_lock<std::mutex> lock(mutex);
            writing_started = true;
            condition.notify_all();
            if (block_in_write) {
                condition.wait(lock, [this]() {
                    return release_write || cancel_called;
                });
            }
        }
        progress_callback(100);
        return write_error;
    }

    infra::Status CommitUpgrade(const UpgradePackageInfo& package_info) override {
        (void)package_info;
        calls.push_back("commit");
        return commit_error;
    }

    infra::Status CancelUpgrade() override {
        calls.push_back("cancel");
        std::lock_guard<std::mutex> lock(mutex);
        cancel_called = true;
        condition.notify_all();
        return cancel_error;
    }

    infra::Status RebootToApply() override {
        calls.push_back("reboot");
        return reboot_error;
    }

    infra::Status CleanupFailedUpgrade() override {
        calls.push_back("cleanup");
        cleanup_called = true;
        return cleanup_error;
    }

    bool WaitWritingStarted() {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, std::chrono::milliseconds(1000),
                                  [this]() { return writing_started; });
    }

    void ReleaseWrite() {
        std::lock_guard<std::mutex> lock(mutex);
        release_write = true;
        condition.notify_all();
    }

    UpgradePackageInfo info;
    std::string current_version = "1.0.0";
    infra::Status validate_error = infra::Status::kOk;
    infra::Status current_version_error = infra::Status::kOk;
    infra::Status compare_error = infra::Status::kOk;
    infra::Status prepare_error = infra::Status::kOk;
    infra::Status write_error = infra::Status::kOk;
    infra::Status commit_error = infra::Status::kOk;
    infra::Status cancel_error = infra::Status::kOk;
    infra::Status reboot_error = infra::Status::kOk;
    infra::Status cleanup_error = infra::Status::kOk;
    bool block_in_write = false;
    bool writing_started = false;
    bool release_write = false;
    bool cancel_called = false;
    bool cleanup_called = false;
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> calls;
};

live_stream::UpgradeServiceOptions MakeOptions(FakeUpgradePlatform* platform,
                                               FakeEventService* event_service,
                                               FakeLoggerService* logger) {
    live_stream::UpgradeServiceOptions options;
    options.platform = platform;
    options.event_service = event_service;
    options.logger_service = logger;
    options.max_package_size_bytes = 1024;
    options.queue_capacity = 4;
    return options;
}

live_stream::RequestContext MakeContext() {
    live_stream::RequestContext context;
    context.request_id = "req-1";
    context.user_name = "admin";
    context.session_id = "session-1";
    context.client_ip = "127.0.0.1";
    return context;
}

bool WaitForState(live_stream::IUpgradeService* service,
                  UpgradeState state,
                  uint32_t timeout_ms = 1000) {
    const int64_t begin = infra::Time::MonotonicMillis();
    while (infra::Time::MonotonicMillis() - begin <
           static_cast<int64_t>(timeout_ms)) {
        if (service->GetStatus().state == state) {
            return true;
        }
        infra::Time::SleepMillis(10);
    }
    return service->GetStatus().state == state;
}

bool HasCall(const FakeUpgradePlatform& platform, const std::string& call) {
    for (const std::string& entry : platform.calls) {
        if (entry == call) {
            return true;
        }
    }
    return false;
}

int TestSuccessAndConfirmReboot() {
    const std::string package_path = MakePackageFile("upgrade_success", 16);
    FakeUpgradePlatform platform;
    platform.info.version = "2.0.0";
    platform.info.target_model = "ipc";
    platform.info.requires_reboot = true;
    FakeEventService event_service;
    FakeLoggerService logger;
    std::unique_ptr<live_stream::IUpgradeService> service =
        live_stream::CreateUpgradeService(
            MakeOptions(&platform, &event_service, &logger));

    if (service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk) {
        return 1;
    }

    live_stream::UpgradeRequest request;
    request.package_path = package_path;
    request.expected_version = "2.0.0";
    if (service->StartUpgrade(MakeContext(), request) != infra::Status::kOk) {
        return 2;
    }
    if (!WaitForState(service.get(), UpgradeState::kWaitingReboot)) {
        return 3;
    }
    if (!HasCall(platform, "validate") || !HasCall(platform, "prepare") ||
        !HasCall(platform, "write") || !HasCall(platform, "commit") ||
        HasCall(platform, "reboot")) {
        return 4;
    }
    if (service->GetStatus().progress_percent != 100 ||
        service->GetStatus().target_version != "2.0.0") {
        return 5;
    }
    if (event_service.events.empty() || logger.records.empty() ||
        logger.records.back().result != live_stream::OperationResult::kSuccess) {
        return 6;
    }

    if (service->ConfirmReboot(MakeContext()) != infra::Status::kOk) {
        return 7;
    }
    if (service->GetStatus().state != UpgradeState::kCompleted ||
        !HasCall(platform, "reboot")) {
        return 8;
    }
    std::remove(package_path.c_str());
    return 0;
}

int TestBusyAndCancel() {
    const std::string package_path = MakePackageFile("upgrade_cancel", 16);
    FakeUpgradePlatform platform;
    platform.info.version = "2.0.0";
    platform.block_in_write = true;
    FakeEventService event_service;
    FakeLoggerService logger;
    std::unique_ptr<live_stream::IUpgradeService> service =
        live_stream::CreateUpgradeService(
            MakeOptions(&platform, &event_service, &logger));
    if (service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk) {
        return 1;
    }

    live_stream::UpgradeRequest request;
    request.package_path = package_path;
    if (service->StartUpgrade(MakeContext(), request) != infra::Status::kOk) {
        return 2;
    }
    if (!platform.WaitWritingStarted()) {
        return 3;
    }
    if (service->StartUpgrade(MakeContext(), request) != infra::Status::kBusy) {
        return 4;
    }
    if (service->CancelUpgrade(MakeContext()) != infra::Status::kOk) {
        return 5;
    }
    platform.ReleaseWrite();
    if (!WaitForState(service.get(), UpgradeState::kCanceled)) {
        return 6;
    }
    if (!platform.cancel_called ||
        service->GetStatus().current_stage != "canceled") {
        return 7;
    }
    std::remove(package_path.c_str());
    return 0;
}

int TestPolicyAndFailureCleanup() {
    const std::string package_path = MakePackageFile("upgrade_policy", 16);
    FakeUpgradePlatform platform;
    platform.info.version = "1.0.0";
    platform.current_version = "1.0.0";
    FakeEventService event_service;
    FakeLoggerService logger;
    std::unique_ptr<live_stream::IUpgradeService> service =
        live_stream::CreateUpgradeService(
            MakeOptions(&platform, &event_service, &logger));
    if (service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk) {
        return 1;
    }

    live_stream::UpgradeRequest request;
    request.package_path = package_path;
    if (service->StartUpgrade(MakeContext(), request) != infra::Status::kOk) {
        return 2;
    }
    if (!WaitForState(service.get(), UpgradeState::kFailed)) {
        return 3;
    }
    if (service->GetStatus().status != infra::Status::kAlreadyExists ||
        logger.records.empty() ||
        logger.records.back().result != live_stream::OperationResult::kRejected) {
        return 4;
    }

    platform.info.version = "2.0.0";
    platform.prepare_error = infra::Status::kIoError;
    if (service->StartUpgrade(MakeContext(), request) != infra::Status::kOk) {
        return 5;
    }
    if (!WaitForState(service.get(), UpgradeState::kFailed)) {
        return 6;
    }
    if (!platform.cleanup_called ||
        service->GetStatus().status != infra::Status::kIoError) {
        return 7;
    }

    if (service->StartUpgrade(MakeContext(), live_stream::UpgradeRequest{}) !=
        infra::Status::kInvalidParam) {
        return 8;
    }
    std::remove(package_path.c_str());
    return 0;
}

int TestDefaultPlatformIsRestricted() {
    const std::string package_path = MakePackageFile("upgrade_restricted", 16);
    live_stream::UpgradeServiceOptions options;
    options.max_package_size_bytes = 1024;
    std::unique_ptr<live_stream::IUpgradeService> service =
        live_stream::CreateUpgradeService(options);
    if (service->Init() != infra::Status::kOk ||
        service->Start() != infra::Status::kOk) {
        return 1;
    }
    if (service->ValidatePackage(package_path).status !=
        infra::Status::kNotSupported) {
        return 2;
    }
    live_stream::UpgradeRequest request;
    request.package_path = package_path;
    if (service->StartUpgrade(MakeContext(), request) != infra::Status::kOk) {
        return 3;
    }
    if (!WaitForState(service.get(), UpgradeState::kFailed)) {
        return 4;
    }
    if (service->GetStatus().status != infra::Status::kNotSupported) {
        return 5;
    }
    std::remove(package_path.c_str());
    return 0;
}

}  // namespace

int main() {
    if (std::strcmp(live_stream::UpgradeService::Name(), "upgrade_service") !=
        0) {
        return 1;
    }
    if (TestSuccessAndConfirmReboot() != 0) {
        return 2;
    }
    if (TestBusyAndCancel() != 0) {
        return 3;
    }
    if (TestPolicyAndFailureCleanup() != 0) {
        return 4;
    }
    if (TestDefaultPlatformIsRestricted() != 0) {
        return 5;
    }
    return 0;
}
