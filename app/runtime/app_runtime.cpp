#include "runtime/app_runtime.h"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/ucontext.h>
#include <thread>
#include <unistd.h>

#include "infra/log.h"
#include "platform/linux/platform_factory.h"
#include "subsystems/core_subsystem.h"
#include "subsystems/device_subsystem.h"
#include "subsystems/media_subsystem.h"
#include "subsystems/protocol_subsystem.h"

namespace live_stream {
namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void HandleSignal(int) { g_stop_requested = 1; }

void DumpMaps() {
    const int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) {
        return;
    }
    char buffer[512];
    while (true) {
        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        (void)write(STDERR_FILENO, buffer, static_cast<size_t>(n));
    }
    (void)close(fd);
}

void HandleSegv(int sig, siginfo_t *info, void *context) {
    const ucontext_t *uc = static_cast<const ucontext_t *>(context);
    unsigned long pc = 0;
    unsigned long lr = 0;
    unsigned long sp = 0;
#if defined(__arm__)
    if (uc != nullptr) {
        pc = uc->uc_mcontext.arm_pc;
        lr = uc->uc_mcontext.arm_lr;
        sp = uc->uc_mcontext.arm_sp;
    }
#else
    (void)uc;
#endif
    char buffer[256];
    const int n = std::snprintf(
        buffer, sizeof(buffer),
        "SIGSEGV diag sig=%d fault=%p pc=0x%08lx lr=0x%08lx sp=0x%08lx\n",
        sig, info != nullptr ? info->si_addr : nullptr, pc, lr, sp);
    if (n > 0) {
        const size_t size =
            static_cast<size_t>(n) < sizeof(buffer) ? static_cast<size_t>(n)
                                                    : sizeof(buffer) - 1;
        (void)write(STDERR_FILENO, buffer, size);
    }
    DumpMaps();
    _exit(128 + sig);
}

}  // namespace

void RequestAppStop() { g_stop_requested = 1; }

void InstallAppSignalHandlers() {
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    struct sigaction action {};
    action.sa_sigaction = HandleSegv;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    (void)sigaction(SIGSEGV, &action, nullptr);
}

AppRuntime &AppRuntime::Get() {
    static AppRuntime runtime;
    return runtime;
}

bool AppRuntime::Start(const RuntimePaths &paths,
                       const char *static_root_override) {
    if (started_) {
        return true;
    }

    CoreSubsystem &core_subsystem = CoreSubsystem::Get();
    DeviceSubsystem &device_subsystem = DeviceSubsystem::Get();
    MediaSubsystem &media_subsystem = MediaSubsystem::Get();
    ProtocolSubsystem &protocol_subsystem = ProtocolSubsystem::Get();

    if (!core_subsystem.Start(paths)) {
        Error("app", "Start core subsystem failed");
        Stop();
        return false;
    }

    AppRuntimeConfig runtime_config;
    if (!LoadRuntimeConfig(core_subsystem.config(), &runtime_config)) {
        Error("app", "Load runtime config failed");
        Stop();
        return false;
    }
    if (static_root_override != nullptr && static_root_override[0] != '\0') {
        runtime_config.static_root = static_root_override;
    }
    runtime_config_ = runtime_config;
    Info("app",
                   "Runtime config: http=%u rtsp=%u onvif=%u "
                   "advertise=%s static_root=%s",
                   static_cast<unsigned>(runtime_config_.http_port),
                   static_cast<unsigned>(runtime_config_.rtsp_port),
                   static_cast<unsigned>(runtime_config_.onvif_device_port),
                   runtime_config_.advertise_host.c_str(),
                   runtime_config_.static_root.c_str());

    if (!device_subsystem.Start(
            core_subsystem,
            CreateLinuxPlatformAdapters(runtime_config_.network_ifname))) {
        Error("app", "Start device subsystem failed");
        Stop();
        return false;
    }
    if (!media_subsystem.Start(core_subsystem, device_subsystem.refs())) {
        Error("app", "Start media subsystem failed");
        Stop();
        return false;
    }
    if (!protocol_subsystem.Start(runtime_config_, core_subsystem,
                                  device_subsystem.refs(),
                                  media_subsystem.refs())) {
        Error("app", "Start protocol subsystem failed");
        Stop();
        return false;
    }

    started_ = true;
    return true;
}

void AppRuntime::Stop() {
    Info("app", "Stop protocol subsystem begin");
    ProtocolSubsystem::Get().Stop();
    Info("app", "Stop protocol subsystem done");
    Info("app", "Stop media subsystem begin");
    MediaSubsystem::Get().Stop();
    Info("app", "Stop media subsystem done");
    Info("app", "Stop device subsystem begin");
    DeviceSubsystem::Get().Stop();
    Info("app", "Stop device subsystem done");
    Info("app", "Stop core subsystem begin");
    CoreSubsystem::Get().Stop();
    Info("app", "Stop core subsystem done");
    started_ = false;
}

void AppRuntime::RunUntilSignal() {
    while (g_stop_requested == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    Info("app", "stop signal received");
}

}  // namespace live_stream
