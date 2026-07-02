#include "json.h"
#include "infra/clamp.h"
#include "infra/fs.h"
#include "infra/hash.h"
#include "infra/time.h"
#include "platform/linux/linux_process.h"
#include "platform/linux/upgrade_status_file.h"
#include "tools/sysupgrade/upgrade_flash.h"
#include "system/package.h"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <pthread.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace live_stream {
namespace {

using linux_platform::RunCommand;
using linux_platform::AppendUpgradeLogLine;

constexpr const char* kDefaultStagePath = "/tmp/live_stream/upgrade/staged";
constexpr const char* kStagePathPrefix = "/tmp/live_stream/upgrade/staged/";
constexpr const char* kUploadPathPrefix = "/tmp/live_stream/upgrade/uploads/";
constexpr uint16_t kDefaultStatusPort = 80;
constexpr int kStatusServerRetryMs = 200;
constexpr int kStatusRequestMaxBytes = 2048;

struct UpgradeRuntimeStatus {
    std::string state = "preparing";
    uint32_t progress_percent = 0;
    std::string current_stage = "preparing";
    std::string version;
    bool ok = true;
    std::string error_message;
    int64_t started_at_ms = 0;
    int64_t finished_at_ms = 0;
};

struct SysupgradeOptions {
    std::string package_path;
    std::string stage_dir = kDefaultStagePath;
    uint16_t status_port = kDefaultStatusPort;
    bool reboot = false;
};

std::mutex g_status_mutex;
UpgradeRuntimeStatus g_status;
std::atomic<bool> g_reboot_requested(false);

bool HasPrefix(const std::string& value, const std::string& prefix) {
    return value.compare(0, prefix.size(), prefix) == 0;
}

bool RemovePathTree(const std::string& path) {
    struct stat path_stat;
    if (path.empty() || lstat(path.c_str(), &path_stat) != 0) {
        return false;
    }
    if (!S_ISDIR(path_stat.st_mode)) {
        return unlink(path.c_str()) == 0;
    }

    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        return false;
    }
    bool ok = true;
    while (true) {
        errno = 0;
        struct dirent* entry = readdir(dir);
        if (entry == nullptr) {
            if (errno != 0) {
                ok = false;
            }
            break;
        }
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        if (!RemovePathTree(infra::Path::Join(path, name))) {
            ok = false;
        }
    }
    if (closedir(dir) != 0) {
        ok = false;
    }
    if (rmdir(path.c_str()) != 0) {
        ok = false;
    }
    return ok;
}

void CleanupTmpUpgradeFiles(const std::string& package_path,
                            const std::string& stage_dir) {
    if (HasPrefix(stage_dir, kStagePathPrefix)) {
        if (RemovePathTree(stage_dir)) {
            AppendUpgradeLogLine("cleanup stage dir: " + stage_dir);
        } else {
            AppendUpgradeLogLine("cleanup stage dir failed: " + stage_dir);
        }
    }
    if (HasPrefix(package_path, kUploadPathPrefix)) {
        if (infra::File::Remove(package_path)) {
            AppendUpgradeLogLine("cleanup upload package: " + package_path);
        } else {
            AppendUpgradeLogLine("cleanup upload package failed: " + package_path);
        }
    }
}

Json StatusToJson(const UpgradeRuntimeStatus& status) {
    Json root = Json::object();
    root["state"] = status.state;
    root["progress_percent"] = status.progress_percent;
    root["current_stage"] =
        status.current_stage.empty() ? status.state : status.current_stage;
    root["ok"] = status.ok;
    root["version"] = status.version;
    root["target_version"] = status.version;
    root["error_message"] = status.error_message;
    root["started_at_ms"] = status.started_at_ms;
    root["finished_at_ms"] = status.finished_at_ms;
    return root;
}

Json StatusEnvelopeToJson(const UpgradeRuntimeStatus& status) {
    Json root = Json::object();
    root["ok"] = true;
    root["data"] = StatusToJson(status);
    root["request_id"] = "";
    return root;
}

Json ErrorEnvelopeToJson(const std::string& code, const std::string& message) {
    Json root = Json::object();
    root["ok"] = false;
    Json error = Json::object();
    error["code"] = code;
    error["message"] = message;
    root["error"] = error;
    root["request_id"] = "";
    return root;
}

UpgradeRuntimeStatus SnapshotUpgradeStatus() {
    std::lock_guard<std::mutex> lock(g_status_mutex);
    return g_status;
}

void UpdateUpgradeStatus(const std::string& state,
                         uint32_t progress,
                         bool ok,
                         const std::string& version,
                         const std::string& current_stage,
                         const std::string& error_message) {
    const uint32_t bounded_progress =
        infra::Clamp<uint32_t>(progress, 0U, 100U);
    std::lock_guard<std::mutex> lock(g_status_mutex);
    g_status.state = state;
    g_status.progress_percent = bounded_progress;
    g_status.current_stage = current_stage.empty() ? state : current_stage;
    g_status.version = version;
    g_status.ok = ok;
    g_status.error_message = error_message;
    if (g_status.started_at_ms == 0) {
        g_status.started_at_ms = infra::Time::SystemTimeMillis();
    }
    if (state == "completed" || state == "failed" || state == "canceled") {
        g_status.finished_at_ms = infra::Time::SystemTimeMillis();
    }
}

void PersistUpgradeStatus(const UpgradeRuntimeStatus& status) {
    static_cast<void>(linux_platform::WriteUpgradeStatusFile(
        StatusToJson(status)));
}

void PersistCurrentUpgradeStatus() {
    PersistUpgradeStatus(SnapshotUpgradeStatus());
}

void WriteUpgradeInfo(const std::string& state,
                      uint32_t progress,
                      bool ok,
                      const std::string& version,
                      const std::string& current_stage,
                      const std::string& error_message) {
    UpdateUpgradeStatus(state, progress, ok, version, current_stage,
                        error_message);
    PersistCurrentUpgradeStatus();
}

void UpdateUpgradeInfo(const std::string& state,
                       uint32_t progress,
                       bool ok,
                       const std::string& version,
                       const std::string& current_stage,
                       const std::string& error_message) {
    UpdateUpgradeStatus(state, progress, ok, version, current_stage,
                        error_message);
}

void PersistCompletedForBoot(const std::string& version) {
    UpgradeRuntimeStatus status = SnapshotUpgradeStatus();
    status.state = "completed";
    status.progress_percent = 100;
    status.current_stage = "upgrade completed";
    status.version = version;
    status.ok = true;
    status.error_message.clear();
    if (status.started_at_ms == 0) {
        status.started_at_ms = infra::Time::SystemTimeMillis();
    }
    status.finished_at_ms = infra::Time::SystemTimeMillis();
    PersistUpgradeStatus(status);
}

void WriteHttpResponse(int client_fd,
                       int status_code,
                       const std::string& reason,
                       const std::string& content_type,
                       const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << reason << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "Cache-Control: no-store\r\n\r\n"
             << body;
    const std::string data = response.str();
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written =
            send(client_fd, data.data() + offset, data.size() - offset,
                 MSG_NOSIGNAL);
        if (written <= 0) {
            return;
        }
        offset += static_cast<std::size_t>(written);
    }
}

void HandleStatusClient(int client_fd) {
    char buffer[kStatusRequestMaxBytes + 1] = {};
    const ssize_t read_size = recv(client_fd, buffer, kStatusRequestMaxBytes, 0);
    if (read_size <= 0) {
        return;
    }
    buffer[read_size] = '\0';
    std::istringstream request(buffer);
    std::string method;
    std::string path;
    std::string version;
    request >> method >> path >> version;
    if (method == "GET" && path == "/api/upgrade/status") {
        const std::string body =
            StatusEnvelopeToJson(SnapshotUpgradeStatus()).dump();
        WriteHttpResponse(client_fd, 200, "OK", "application/json", body);
        return;
    }
    if (method == "GET" && path == "/api/health") {
        WriteHttpResponse(client_fd, 200, "OK", "text/plain", "upgrading\n");
        return;
    }
    if (method == "POST" && path == "/api/upgrade/confirm-reboot") {
        const UpgradeRuntimeStatus status = SnapshotUpgradeStatus();
        if (status.state != "waiting_reboot") {
            WriteHttpResponse(
                client_fd, 409, "Conflict", "application/json",
                ErrorEnvelopeToJson("reboot_not_pending",
                                    "reboot is not pending")
                    .dump());
            return;
        }
        UpdateUpgradeInfo("completed", 100, true, status.version,
                          "all flash partitions written; rebooting", "");
        g_reboot_requested.store(true);
        WriteHttpResponse(client_fd, 200, "OK", "application/json",
                          StatusEnvelopeToJson(SnapshotUpgradeStatus()).dump());
        return;
    }
    if (method != "GET" && method != "POST") {
        WriteHttpResponse(
            client_fd, 405, "Method Not Allowed", "application/json",
            ErrorEnvelopeToJson("method_not_allowed", "method not allowed")
                .dump());
        return;
    }
    WriteHttpResponse(
        client_fd, 503, "Service Unavailable", "application/json",
        ErrorEnvelopeToJson("upgrade_in_progress", "upgrade in progress")
            .dump());
}

void StatusServerLoop(uint16_t port) {
    int listen_fd = -1;
    while (listen_fd < 0) {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) {
            infra::Time::SleepMillis(kStatusServerRetryMs);
            continue;
        }
        int reuse = 1;
        static_cast<void>(
            setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                       sizeof(reuse)));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(port);
        if (bind(listen_fd, reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)) == 0 &&
            listen(listen_fd, 4) == 0) {
            break;
        }
        close(listen_fd);
        listen_fd = -1;
        infra::Time::SleepMillis(kStatusServerRetryMs);
    }
    AppendUpgradeLogLine("status server listening: port=" + std::to_string(port));
    while (true) {
        const int client_fd = accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            infra::Time::SleepMillis(kStatusServerRetryMs);
            continue;
        }
        HandleStatusClient(client_fd);
        close(client_fd);
    }
}

void* StatusServerThread(void* arg) {
    const uint16_t port =
        static_cast<uint16_t>(reinterpret_cast<uintptr_t>(arg));
    StatusServerLoop(port);
    return nullptr;
}

bool StartStatusServer(uint16_t port) {
    pthread_t thread = 0;
    const int rc = pthread_create(
        &thread, nullptr, StatusServerThread,
        reinterpret_cast<void*>(static_cast<uintptr_t>(port)));
    if (rc != 0) {
        AppendUpgradeLogLine("status server thread create failed: errno=" +
                         std::to_string(rc));
        return false;
    }
    static_cast<void>(pthread_detach(thread));
    return true;
}

void Usage() {
    AppendUpgradeLogLine(
        "usage: live_sysupgrade --package <upgrade.zip> [--stage <dir>] "
        "[--status-port <port>] [--reboot]");
}

bool ParseArgs(int argc, char** argv, SysupgradeOptions* options) {
    if (options == nullptr) {
        return false;
    }
    SysupgradeOptions parsed;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--package" && i + 1 < argc) {
            parsed.package_path = argv[++i];
        } else if (arg == "--stage" && i + 1 < argc) {
            parsed.stage_dir = argv[++i];
        } else if (arg == "--status-port" && i + 1 < argc) {
            const long port = std::strtol(argv[++i], nullptr, 10);
            if (port <= 0 || port > 65535) {
                return false;
            }
            parsed.status_port = static_cast<uint16_t>(port);
        } else if (arg == "--reboot") {
            parsed.reboot = true;
        } else {
            return false;
        }
    }
    if (parsed.package_path.empty() || parsed.stage_dir.empty()) {
        return false;
    }
    *options = parsed;
    return true;
}

void StopLiveStreamApp() {
    AppendUpgradeLogLine("stop application begin");
    static_cast<void>(RunCommand({"/etc/init.d/S80live_stream", "stop"}));
    static_cast<void>(RunCommand({"killall", "live_stream"}));
    infra::Time::SleepMillis(300);
    AppendUpgradeLogLine("stop application done");
}

bool UnmountForCommand(const UpgradeCommand& command, std::string* msg) {
    if (command.partition == "web" || command.partition == "bin" ||
        command.partition == "config") {
        return upgrade_flash::UnmountIfMounted(
            command.partition_info.mount_point, msg);
    }
    return true;
}

bool PackageNeedsStoppedApp(const UpgradeManifest& manifest) {
    for (const UpgradeCommand& command : manifest.commands) {
        if (command.partition != "web") {
            return true;
        }
    }
    return false;
}

bool ApplyPackage(const ParsedUpgradePackage& package,
                  const std::string& stage_dir,
                  std::string* msg) {
    AppendUpgradeLogLine("apply package begin: stage=" + stage_dir +
                     " commands=" +
                     std::to_string(package.manifest.commands.size()));
    for (const UpgradeCommand& command : package.manifest.commands) {
        if (command.partition == "rootfs") {
            if (msg != nullptr) {
                *msg = "rootfs online upgrade is disabled";
            }
            return false;
        }
    }
    AppendUpgradeLogLine("apply package rootfs check done");
    WriteUpgradeInfo("preparing", 6, true, package.manifest.version,
                     "checking temporary upgrade workspace", "");
    if (!upgrade_flash::IsPathOnTmpfs(stage_dir)) {
        if (msg != nullptr) {
            *msg = "stage directory is not tmpfs";
        }
        return false;
    }
    AppendUpgradeLogLine("apply package tmpfs check done");
    WriteUpgradeInfo("preparing", 7, true, package.manifest.version,
                     "checking flash partition layout", "");
    if (!upgrade_flash::ValidateMtdLayoutForManifest(package.manifest, msg)) {
        return false;
    }
    AppendUpgradeLogLine("apply package mtd layout check done");
    WriteUpgradeInfo("preparing", 8, true, package.manifest.version,
                     "creating temporary write workspace", "");
    if (!infra::Path::MakeDirs(stage_dir)) {
        if (msg != nullptr) {
            *msg = "create stage directory failed";
        }
        return false;
    }
    AppendUpgradeLogLine("apply package stage dir ready");
    WriteUpgradeInfo("writing", 9, true, package.manifest.version,
                     "extracting upgrade package", "");

    uint32_t last_persisted_extract_progress = 9;
    bool extract_ok = ExtractUpgradeFiles(
        package.package_path, package.manifest, stage_dir,
        [&package, &last_persisted_extract_progress](uint32_t progress) {
            const uint32_t bounded = infra::Clamp<uint32_t>(progress, 0U, 100U);
            const uint32_t mapped_progress =
                9U + static_cast<uint32_t>((bounded * 16ULL) / 100U);
            if (mapped_progress >= last_persisted_extract_progress + 5U ||
                mapped_progress >= 25U) {
                last_persisted_extract_progress = mapped_progress;
                WriteUpgradeInfo("writing", mapped_progress, true,
                                 package.manifest.version,
                                 "extracting upgrade package", "");
                return;
            }
            UpdateUpgradeInfo("writing", mapped_progress, true,
                              package.manifest.version,
                              "extracting upgrade package", "");
        },
        msg);
    if (!extract_ok) {
        return false;
    }
    AppendUpgradeLogLine("apply package extract done");

    if (PackageNeedsStoppedApp(package.manifest)) {
        AppendUpgradeLogLine("stop application before system upgrade");
        StopLiveStreamApp();
    }

    for (std::size_t i = 0; i < package.manifest.commands.size(); ++i) {
        const UpgradeCommand& command = package.manifest.commands[i];
        const std::string ordinal =
            std::to_string(i + 1) + "/" +
            std::to_string(package.manifest.commands.size());
        const std::string begin_stage =
            "writing flash " + command.partition + " (" + ordinal + ")";
        const uint32_t begin_progress = 25U + static_cast<uint32_t>(
            (i * 70ULL) / package.manifest.commands.size());
        const uint32_t end_progress = 25U + static_cast<uint32_t>(
            ((i + 1) * 70ULL) / package.manifest.commands.size());
        AppendUpgradeLogLine("burn partition begin: " + command.partition +
                         " index=" + ordinal + " file=" + command.file);
        WriteUpgradeInfo("writing",
                         infra::Clamp<uint32_t>(begin_progress, 25U, 95U),
                         true, package.manifest.version, begin_stage, "");
        if (!UnmountForCommand(command, msg)) {
            return false;
        }
        const std::string image_path = infra::Path::Join(stage_dir, command.file);
        if (!upgrade_flash::WriteMtdImage(
                command, image_path,
                [&package, &command, &ordinal, begin_progress, end_progress](
                    uint32_t mtd_progress, const std::string& stage) {
                    const uint32_t bounded =
                        infra::Clamp<uint32_t>(mtd_progress, 0U, 100U);
                    const uint32_t range =
                        end_progress > begin_progress
                            ? end_progress - begin_progress
                            : 0U;
                    const uint32_t progress =
                        begin_progress +
                        static_cast<uint32_t>((bounded * range) / 100U);
                    UpdateUpgradeInfo(
                        "writing",
                        infra::Clamp<uint32_t>(progress, 25U, 95U), true,
                        package.manifest.version,
                        "writing flash " + command.partition + " (" +
                            ordinal + "): " + stage,
                        "");
                },
                msg)) {
            return false;
        }
        const uint32_t progress = end_progress;
        AppendUpgradeLogLine("burn partition done: " + command.partition +
                         " index=" + ordinal + " progress=" +
                         std::to_string(progress));
        WriteUpgradeInfo("writing", infra::Clamp<uint32_t>(progress, 25U, 95U),
                         true, package.manifest.version,
                         "flash " + command.partition + " done (" +
                             ordinal + ")",
                         "");
    }
    return true;
}

int Run(int argc, char** argv) {
    SysupgradeOptions options;
    if (!ParseArgs(argc, argv, &options)) {
        Usage();
        return 2;
    }

    ParsedUpgradePackage package;
    std::string msg;
    if (!ParseUpgradePackage(options.package_path, &package, &msg)) {
        AppendUpgradeLogLine("sysupgrade validate failed: msg=" + msg);
        WriteUpgradeInfo("failed", SnapshotUpgradeStatus().progress_percent,
                         false, "", "validate failed", msg);
        return 1;
    }

    AppendUpgradeLogLine("sysupgrade started: version=" +
                         package.manifest.version + " exe=" + argv[0] +
                         " exe_sha256=" + infra::Sha256FileHex(argv[0]) +
                         " package=" + options.package_path +
                         " stage=" + options.stage_dir);
    WriteUpgradeInfo("preparing", 5, true, package.manifest.version,
                     "sysupgrade helper preparing", "");
    const bool status_server_started = StartStatusServer(options.status_port);
    if (!status_server_started && !options.reboot) {
        AppendUpgradeLogLine(
            "sysupgrade failed: status server is required without auto reboot");
        WriteUpgradeInfo("failed", SnapshotUpgradeStatus().progress_percent,
                         false, package.manifest.version,
                         "status server start failed",
                         "status server is required without auto reboot");
        return 1;
    }

    if (!ApplyPackage(package, options.stage_dir, &msg)) {
        AppendUpgradeLogLine("sysupgrade failed: msg=" + msg);
        WriteUpgradeInfo("failed", SnapshotUpgradeStatus().progress_percent,
                         false, package.manifest.version,
                         "sysupgrade failed", msg);
        CleanupTmpUpgradeFiles(options.package_path, options.stage_dir);
        sync();
        return 1;
    }

    AppendUpgradeLogLine("sysupgrade completed: " + package.manifest.version);
    WriteUpgradeInfo("writing", 96, true, package.manifest.version,
                     "cleaning temporary upgrade files", "");
    CleanupTmpUpgradeFiles(options.package_path, options.stage_dir);
    const bool will_reboot = options.reboot;
    if (will_reboot) {
        UpdateUpgradeInfo("completed", 100, true, package.manifest.version,
                          "all flash partitions written; rebooting", "");
        PersistCompletedForBoot(package.manifest.version);
        sync();
        reboot(RB_AUTOBOOT);
        reboot(RB_POWER_OFF);
        AppendUpgradeLogLine("reboot syscall failed after completed upgrade");
        WriteUpgradeInfo("failed", 100, false, package.manifest.version,
                         "reboot failed", "reboot failed");
        return 1;
    }
    WriteUpgradeInfo("waiting_reboot", 100, true, package.manifest.version,
                     "all flash partitions written; waiting reboot", "");
    while (!g_reboot_requested.load()) {
        infra::Time::SleepMillis(200);
    }
    AppendUpgradeLogLine("reboot confirmed by upgrade status server");
    UpdateUpgradeInfo("completed", 100, true, package.manifest.version,
                      "all flash partitions written; rebooting", "");
    PersistCompletedForBoot(package.manifest.version);
    sync();
    reboot(RB_AUTOBOOT);
    reboot(RB_POWER_OFF);
    AppendUpgradeLogLine("reboot syscall failed after confirmed upgrade");
    WriteUpgradeInfo("failed", 100, false, package.manifest.version,
                     "reboot failed", "reboot failed");
    return 1;
}

}  // namespace
}  // namespace live_stream

int main(int argc, char** argv) {
    return live_stream::Run(argc, argv);
}
