#include "infra/clamp.h"
#include "infra/fs.h"
#include "infra/time.h"
#include "linux_platform_common.h"
#include "upgrade_flash.h"
#include "upgrade_package.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <sys/reboot.h>
#include <unistd.h>
#include <vector>

namespace live_stream {
namespace {

using linux_platform::RunCommand;

constexpr const char* kUpgradeLogPath = "/data/upgrade.log";
constexpr const char* kUpgradeStatusPath = "/data/upgrade_status.json";
constexpr const char* kDefaultStagePath = "/tmp/live_stream/upgrade/staged";

struct HelperOptions {
    std::string package_path;
    std::string stage_dir = kDefaultStagePath;
    bool reboot = false;
};

std::string JsonEscape(const std::string& value) {
    std::string escaped;
    for (char c : value) {
        if (c == '"' || c == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

void AppendUpgradeLog(const std::string& message) {
    static_cast<void>(infra::Path::MakeDirs("/data"));
    const std::string line =
        std::to_string(infra::Time::SystemTimeMillis()) + " " + message + "\n";
    static_cast<void>(infra::File::Append(kUpgradeLogPath, line));
}

void WriteUpgradeStatus(const std::string& state,
                        uint32_t progress,
                        bool ok,
                        const std::string& version,
                        const std::string& error_message) {
    static_cast<void>(infra::Path::MakeDirs("/data"));
    std::ostringstream out;
    out << "{\n"
        << "  \"state\": \"" << JsonEscape(state) << "\",\n"
        << "  \"progress_percent\": " << progress << ",\n"
        << "  \"ok\": " << (ok ? "true" : "false") << ",\n"
        << "  \"version\": \"" << JsonEscape(version) << "\",\n"
        << "  \"error_message\": \"" << JsonEscape(error_message) << "\"\n"
        << "}\n";
    static_cast<void>(infra::File::WriteAll(kUpgradeStatusPath, out.str()));
}

void Usage() {
    AppendUpgradeLog(
        "usage: live_sysupgrade --package <upgrade.zip> [--stage <dir>] "
        "[--reboot]");
}

bool ParseArgs(int argc, char** argv, HelperOptions* options) {
    if (options == nullptr) {
        return false;
    }
    HelperOptions parsed;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--package" && i + 1 < argc) {
            parsed.package_path = argv[++i];
        } else if (arg == "--stage" && i + 1 < argc) {
            parsed.stage_dir = argv[++i];
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

bool StopAppServices() {
    static_cast<void>(RunCommand({"/etc/init.d/S80live_stream", "stop"}));
    static_cast<void>(RunCommand({"killall", "live_stream"}));
    static_cast<void>(RunCommand({"pkill", "live_stream"}));
    infra::Time::SleepMillis(300);
    return true;
}

bool UnmountForCommand(const UpgradeCommand& command, std::string* reason) {
    if (command.partition == "web" || command.partition == "bin" ||
        command.partition == "config") {
        return upgrade_flash::UnmountIfMounted(
            command.partition_info.mount_point, reason);
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
                  std::string* reason) {
    if (!upgrade_flash::IsPathOnTmpfs(stage_dir)) {
        if (reason != nullptr) {
            *reason = "stage directory is not tmpfs";
        }
        return false;
    }
    if (!upgrade_flash::ValidateMtdLayoutForManifest(package.manifest, reason)) {
        return false;
    }
    if (!infra::Path::MakeDirs(stage_dir)) {
        if (reason != nullptr) {
            *reason = "create stage directory failed";
        }
        return false;
    }

    bool extract_ok = ExtractUpgradeFiles(
        package.package_path, package.manifest, stage_dir,
        [&package](uint32_t progress) {
            const uint32_t bounded = infra::Clamp<uint32_t>(progress, 0U, 100U);
            WriteUpgradeStatus("writing", bounded / 4, true,
                               package.manifest.version, "");
        },
        reason);
    if (!extract_ok) {
        return false;
    }

    if (PackageNeedsStoppedApp(package.manifest)) {
        AppendUpgradeLog("stop application before system upgrade");
        StopAppServices();
    }

    for (std::size_t i = 0; i < package.manifest.commands.size(); ++i) {
        const UpgradeCommand& command = package.manifest.commands[i];
        AppendUpgradeLog("burn partition: " + command.partition);
        if (!UnmountForCommand(command, reason)) {
            return false;
        }
        const std::string image_path = infra::Path::Join(stage_dir, command.file);
        if (!upgrade_flash::WriteMtdImage(command, image_path, reason)) {
            return false;
        }
        const uint32_t progress = 25U + static_cast<uint32_t>(
            ((i + 1) * 70ULL) / package.manifest.commands.size());
        WriteUpgradeStatus("writing", infra::Clamp<uint32_t>(progress, 25U, 95U),
                           true, package.manifest.version, "");
    }
    return true;
}

int Run(int argc, char** argv) {
    HelperOptions options;
    if (!ParseArgs(argc, argv, &options)) {
        Usage();
        return 2;
    }

    ParsedUpgradePackage package;
    std::string reason;
    if (!ParseUpgradePackage(options.package_path, &package, &reason)) {
        AppendUpgradeLog("helper validate failed: " + reason);
        WriteUpgradeStatus("failed", 100, false, "", reason);
        return 1;
    }

    AppendUpgradeLog("helper started: " + package.manifest.version);
    WriteUpgradeStatus("preparing", 5, true, package.manifest.version, "");

    if (!ApplyPackage(package, options.stage_dir, &reason)) {
        AppendUpgradeLog("helper failed: " + reason);
        WriteUpgradeStatus("failed", 100, false, package.manifest.version,
                           reason);
        sync();
        return 1;
    }

    AppendUpgradeLog("helper completed: " + package.manifest.version);
    WriteUpgradeStatus("waiting_reboot", 100, true, package.manifest.version, "");
    sync();
    if (options.reboot || package.requires_reboot) {
        reboot(RB_AUTOBOOT);
        reboot(RB_POWER_OFF);
    }
    return 0;
}

}  // namespace
}  // namespace live_stream

int main(int argc, char** argv) {
    return live_stream::Run(argc, argv);
}
