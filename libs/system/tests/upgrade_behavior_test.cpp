#include "upgrade.h"

#include "event.h"
#include "infra/time.h"
#include "logger.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace {

using live_stream::IUpgradePlatform;
using live_stream::UpgradePackageInfo;
using live_stream::UpgradeProgressCallback;
using live_stream::UpgradeState;

std::string MakePackageFile(const char* name, std::size_t size) {
    std::string path = "/tmp/live_stream/upgrade/uploads/";
    path += name;
    path += "_";
    path += std::to_string(static_cast<long long>(getpid()));
    static_cast<void>(mkdir("/tmp/live_stream", 0755));
    static_cast<void>(mkdir("/tmp/live_stream/upgrade", 0755));
    static_cast<void>(mkdir("/tmp/live_stream/upgrade/uploads", 0755));
    std::ofstream output(path, std::ios::binary);
    for (std::size_t i = 0; i < size; ++i) {
        output.put('x');
    }
    return path;
}

struct FakeEvent : live_stream::event::Dispatcher {
    FakeEvent()
        : subscription(Subscribe(
              live_stream::event::EventType::kUpgradeProgressChanged,
              [this](const live_stream::event::Event& event) {
                  events.push_back(event);
              })) {}

    std::vector<live_stream::event::Event> events;

private:
    live_stream::event::Subscription subscription;
};

struct FakeLogger : live_stream::ILogger {
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    bool RecordOperation(
        const live_stream::OperationRecord& record) override {
        records.push_back(record);
        return true;
    }

    std::vector<live_stream::OperationRecord> QueryOperations(
        const live_stream::OperationLogQuery& query) override {
        (void)query;
        return records;
    }

    bool ExportOperations(
        const live_stream::OperationLogExportOptions& options) override {
        (void)options;
        return true;
    }

    std::vector<live_stream::OperationRecord> records;
};

struct FakeUpgradePlatform : IUpgradePlatform {
    UpgradePackageInfo ValidatePackage(
        const std::string& package_path) override {
        calls.push_back("validate");
        if (!validate_ok) {
            return UpgradePackageInfo();
        }
        UpgradePackageInfo result = info;
        result.package_path = package_path;
        result.size_bytes = 16;
        return result;
    }

    std::string GetCurrentVersion() override {
        calls.push_back("current_version");
        return current_version;
    }

    int CompareVersion(const std::string& lhs,
                       const std::string& rhs) override {
        calls.push_back("compare");
        if (lhs < rhs) {
            return -1;
        }
        if (lhs > rhs) {
            return 1;
        }
        return 0;
    }

    bool PrepareUpgrade(const UpgradePackageInfo& package_info) override {
        (void)package_info;
        calls.push_back("prepare");
        return prepare_ok;
    }

    bool WriteUpgrade(
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
        return write_ok;
    }

    bool CommitUpgrade(const UpgradePackageInfo& package_info) override {
        (void)package_info;
        calls.push_back("commit");
        return commit_ok;
    }

    bool CancelUpgrade() override {
        calls.push_back("cancel");
        std::lock_guard<std::mutex> lock(mutex);
        cancel_called = true;
        condition.notify_all();
        return cancel_ok;
    }

    bool RebootToApply() override {
        calls.push_back("reboot");
        return reboot_ok;
    }

    bool CleanupFailedUpgrade() override {
        calls.push_back("cleanup");
        cleanup_called = true;
        return cleanup_ok;
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
    bool validate_ok = true;
    bool prepare_ok = true;
    bool write_ok = true;
    bool commit_ok = true;
    bool cancel_ok = true;
    bool reboot_ok = true;
    bool cleanup_ok = true;
    bool block_in_write = false;
    bool writing_started = false;
    bool release_write = false;
    bool cancel_called = false;
    bool cleanup_called = false;
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> calls;
};

live_stream::UpgradeOptions MakeOptions(FakeUpgradePlatform* platform,
                                        FakeEvent* event,
                                        FakeLogger* logger) {
    live_stream::UpgradeOptions options;
    options.platform = platform;
    options.event = event;
    options.logger = logger;
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

bool WaitForState(live_stream::IUpgrade* service,
                  UpgradeState state,
                  uint32_t timeout_ms = 1000) {
    const int64_t begin = infra::Time::MonotonicMillis();
    while (infra::Time::MonotonicMillis() - begin <
           static_cast<int64_t>(timeout_ms)) {
        if (service->GetUpgradeInfo().state == state) {
            return true;
        }
        infra::Time::SleepMillis(10);
    }
    return service->GetUpgradeInfo().state == state;
}

bool ContainsCall(const FakeUpgradePlatform& platform, const std::string& call) {
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
    FakeEvent event;
    FakeLogger logger;
    std::unique_ptr<live_stream::IUpgrade> service =
        live_stream::CreateUpgrade(
            MakeOptions(&platform, &event, &logger));

    if (!service->Start()) {
        return 1;
    }

    live_stream::UpgradeRequest request;
    request.package_path = package_path;
    request.expected_version = "2.0.0";
    if (!service->StartUpgrade(MakeContext(), request)) {
        return 2;
    }
    if (!WaitForState(service.get(), UpgradeState::kWaitingReboot)) {
        return 3;
    }
    if (!ContainsCall(platform, "validate") ||
        !ContainsCall(platform, "prepare") ||
        !ContainsCall(platform, "write") ||
        !ContainsCall(platform, "commit") ||
        ContainsCall(platform, "reboot")) {
        return 4;
    }
    if (service->GetUpgradeInfo().progress_percent != 100 ||
        service->GetUpgradeInfo().target_version != "2.0.0") {
        return 5;
    }
    if (event.events.empty() || logger.records.empty() ||
        logger.records.back().result != live_stream::OperationResult::kSuccess) {
        return 6;
    }

    if (!service->ConfirmReboot(MakeContext())) {
        return 7;
    }
    if (service->GetUpgradeInfo().state != UpgradeState::kCompleted ||
        !ContainsCall(platform, "reboot")) {
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
    FakeEvent event;
    FakeLogger logger;
    std::unique_ptr<live_stream::IUpgrade> service =
        live_stream::CreateUpgrade(
            MakeOptions(&platform, &event, &logger));
    if (!service->Start()) {
        return 1;
    }

    live_stream::UpgradeRequest request;
    request.package_path = package_path;
    if (!service->StartUpgrade(MakeContext(), request)) {
        return 2;
    }
    if (!platform.WaitWritingStarted()) {
        return 3;
    }
    if (service->StartUpgrade(MakeContext(), request)) {
        return 4;
    }
    if (!service->CancelUpgrade(MakeContext())) {
        return 5;
    }
    platform.ReleaseWrite();
    if (!WaitForState(service.get(), UpgradeState::kCanceled)) {
        return 6;
    }
    if (!platform.cancel_called ||
        service->GetUpgradeInfo().current_stage != "canceled") {
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
    FakeEvent event;
    FakeLogger logger;
    std::unique_ptr<live_stream::IUpgrade> service =
        live_stream::CreateUpgrade(
            MakeOptions(&platform, &event, &logger));
    if (!service->Start()) {
        return 1;
    }

    live_stream::UpgradeRequest request;
    request.package_path = package_path;
    if (!service->StartUpgrade(MakeContext(), request)) {
        return 2;
    }
    if (!WaitForState(service.get(), UpgradeState::kFailed)) {
        return 3;
    }
    if (service->GetUpgradeInfo().error_message != "same version is not allowed" ||
        logger.records.empty() ||
        logger.records.back().result != live_stream::OperationResult::kRejected) {
        return 4;
    }

    platform.info.version = "2.0.0";
    platform.prepare_ok = false;
    if (!service->StartUpgrade(MakeContext(), request)) {
        return 5;
    }
    if (!WaitForState(service.get(), UpgradeState::kFailed)) {
        return 6;
    }
    if (!platform.cleanup_called ||
        service->GetUpgradeInfo().error_message != "prepare upgrade failed") {
        return 7;
    }

    if (service->StartUpgrade(MakeContext(), live_stream::UpgradeRequest{})) {
        return 8;
    }
    std::remove(package_path.c_str());
    return 0;
}

int TestDefaultPlatformIsRestricted() {
    const std::string package_path = MakePackageFile("upgrade_restricted", 16);
    live_stream::UpgradeOptions options;
    options.max_package_size_bytes = 1024;
    std::unique_ptr<live_stream::IUpgrade> service =
        live_stream::CreateUpgrade(options);
    if (!service->Start()) {
        return 1;
    }
    if (!service->ValidatePackage(package_path).version.empty()) {
        return 2;
    }
    live_stream::UpgradeRequest request;
    request.package_path = package_path;
    if (!service->StartUpgrade(MakeContext(), request)) {
        return 3;
    }
    if (!WaitForState(service.get(), UpgradeState::kFailed)) {
        return 4;
    }
    if (service->GetUpgradeInfo().error_message != "package validation failed") {
        return 5;
    }
    std::remove(package_path.c_str());
    return 0;
}

int TestRejectsPathOutsideUploadDirectory() {
    const std::string package_path = "/tmp/upgrade_reject_" +
                                     std::to_string(static_cast<long long>(getpid()));
    std::ofstream output(package_path, std::ios::binary);
    output.put('x');
    output.close();

    FakeUpgradePlatform platform;
    platform.info.version = "2.0.0";
    FakeEvent event;
    FakeLogger logger;
    std::unique_ptr<live_stream::IUpgrade> service =
        live_stream::CreateUpgrade(
            MakeOptions(&platform, &event, &logger));
    if (!service->Start()) {
        return 1;
    }
    if (!service->ValidatePackage(package_path).version.empty()) {
        return 2;
    }
    live_stream::UpgradeRequest request;
    request.package_path = package_path;
    if (service->StartUpgrade(MakeContext(), request)) {
        return 3;
    }
    if (ContainsCall(platform, "validate")) {
        return 4;
    }
    std::remove(package_path.c_str());
    return 0;
}

int TestRejectsSymlinkInsideUploadDirectory() {
    const std::string real_path = "/tmp/upgrade_real_" +
                                  std::to_string(static_cast<long long>(getpid()));
    {
        std::ofstream output(real_path, std::ios::binary);
        output.put('x');
    }
    const std::string symlink_path = "/tmp/live_stream/upgrade/uploads/" +
                                     std::string("upgrade_symlink_") +
                                     std::to_string(static_cast<long long>(getpid()));
    static_cast<void>(mkdir("/tmp/live_stream", 0755));
    static_cast<void>(mkdir("/tmp/live_stream/upgrade", 0755));
    static_cast<void>(mkdir("/tmp/live_stream/upgrade/uploads", 0755));
    static_cast<void>(std::remove(symlink_path.c_str()));
    if (symlink(real_path.c_str(), symlink_path.c_str()) != 0) {
        std::remove(real_path.c_str());
        return 1;
    }

    FakeUpgradePlatform platform;
    platform.info.version = "2.0.0";
    FakeEvent event;
    FakeLogger logger;
    std::unique_ptr<live_stream::IUpgrade> service =
        live_stream::CreateUpgrade(
            MakeOptions(&platform, &event, &logger));
    if (!service->Start()) {
        std::remove(symlink_path.c_str());
        std::remove(real_path.c_str());
        return 2;
    }
    if (!service->ValidatePackage(symlink_path).version.empty()) {
        std::remove(symlink_path.c_str());
        std::remove(real_path.c_str());
        return 3;
    }
    live_stream::UpgradeRequest request;
    request.package_path = symlink_path;
    if (service->StartUpgrade(MakeContext(), request)) {
        std::remove(symlink_path.c_str());
        std::remove(real_path.c_str());
        return 4;
    }
    if (ContainsCall(platform, "validate")) {
        std::remove(symlink_path.c_str());
        std::remove(real_path.c_str());
        return 5;
    }
    std::remove(symlink_path.c_str());
    std::remove(real_path.c_str());
    return 0;
}

int TestAutoRebootAndCommittedFailure() {
    const std::string package_path = MakePackageFile("upgrade_auto_reboot", 16);
    FakeUpgradePlatform platform;
    platform.info.version = "2.0.0";
    platform.info.requires_reboot = true;
    FakeEvent event;
    FakeLogger logger;
    std::unique_ptr<live_stream::IUpgrade> service =
        live_stream::CreateUpgrade(
            MakeOptions(&platform, &event, &logger));
    if (!service->Start()) {
        return 1;
    }

    live_stream::UpgradeRequest request;
    request.package_path = package_path;
    request.auto_reboot = true;
    if (!service->StartUpgrade(MakeContext(), request)) {
        return 2;
    }
    if (!WaitForState(service.get(), UpgradeState::kCompleted)) {
        return 3;
    }
    if (!ContainsCall(platform, "reboot") ||
        service->GetUpgradeInfo().progress_percent != 100) {
        return 4;
    }

    platform.calls.clear();
    platform.reboot_ok = false;
    if (!service->StartUpgrade(MakeContext(), request)) {
        return 5;
    }
    if (!WaitForState(service.get(), UpgradeState::kFailed)) {
        return 6;
    }
    if (service->GetUpgradeInfo().error_message.find(
            "committed upgrade may require manual recovery") ==
        std::string::npos) {
        return 7;
    }
    std::remove(package_path.c_str());
    return 0;
}

}  // namespace

int main() {
    if (std::strcmp(live_stream::Upgrade::Name(), "upgrade") !=
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
    if (TestRejectsPathOutsideUploadDirectory() != 0) {
        return 6;
    }
    if (TestRejectsSymlinkInsideUploadDirectory() != 0) {
        return 7;
    }
    if (TestAutoRebootAndCommittedFailure() != 0) {
        return 8;
    }
    return 0;
}
