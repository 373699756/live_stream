#include "app_runtime.h"

#include <chrono>
#include <csignal>
#include <thread>

#include "device_subsystem.h"
#include "infra/log.h"
#include "media_subsystem.h"
#include "platform_factory.h"
#include "protocol_subsystem.h"

namespace live_stream {
namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void HandleSignal(int) { g_stop_requested = 1; }

}  // namespace

void RequestAppStop() { g_stop_requested = 1; }

void InstallAppSignalHandlers() {
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
}

AppRuntime &AppRuntime::Get() {
    static AppRuntime runtime;
    return runtime;
}

bool AppRuntime::Start(const RuntimePaths &paths) {
    if (started_) {
        return true;
    }

    if (!CoreServices::Get().Start(paths)) {
        INFRA_LOG_ERROR("app", "Start core services failed");
        Stop();
        return false;
    }

    AppRuntimeConfig runtime_config;
    if (!LoadRuntimeConfig(CoreServices::Get().config(), &runtime_config)) {
        INFRA_LOG_ERROR("app", "Load runtime config failed");
        Stop();
        return false;
    }
    runtime_config_ = runtime_config;
    INFRA_LOG_INFO("app",
                   "Runtime config: http=%u rtsp=%u onvif=%u "
                   "advertise=%s static_root=%s",
                   static_cast<unsigned>(runtime_config_.http_port),
                   static_cast<unsigned>(runtime_config_.rtsp_port),
                   static_cast<unsigned>(runtime_config_.onvif_device_port),
                   runtime_config_.advertise_host.c_str(),
                   runtime_config_.static_root.c_str());

    if (!DeviceSubsystem::Get().Start(
            CreateLinuxPlatformAdapters(runtime_config_.network_ifname))) {
        INFRA_LOG_ERROR("app", "Start device subsystem failed");
        Stop();
        return false;
    }
    if (!MediaSubsystem::Get().Start()) {
        INFRA_LOG_ERROR("app", "Start media subsystem failed");
        Stop();
        return false;
    }
    if (!ProtocolSubsystem::Get().Start(runtime_config_)) {
        INFRA_LOG_ERROR("app", "Start protocol subsystem failed");
        Stop();
        return false;
    }

    started_ = true;
    return true;
}

void AppRuntime::Stop() {
    INFRA_LOG_INFO("app", "Stop protocol subsystem begin");
    ProtocolSubsystem::Get().Stop();
    INFRA_LOG_INFO("app", "Stop protocol subsystem done");
    INFRA_LOG_INFO("app", "Stop media subsystem begin");
    MediaSubsystem::Get().Stop();
    INFRA_LOG_INFO("app", "Stop media subsystem done");
    INFRA_LOG_INFO("app", "Stop device subsystem begin");
    DeviceSubsystem::Get().Stop();
    INFRA_LOG_INFO("app", "Stop device subsystem done");
    INFRA_LOG_INFO("app", "Stop core services begin");
    CoreServices::Get().Stop();
    INFRA_LOG_INFO("app", "Stop core services done");
    started_ = false;
}

void AppRuntime::RunUntilSignal() {
    while (g_stop_requested == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    INFRA_LOG_INFO("app", "stop signal received");
}

}  // namespace live_stream
