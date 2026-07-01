#include "handlers/system_overview_response.h"

#include "http_ai_health.h"

#include "alarm.h"
#include "auth.h"
#include "config.h"
#include "device.h"
#include "logger.h"
#include "media/media_source_registry.h"
#include "media/media_streams.h"
#include "onvif_server.h"
#include "runtime.h"
#include "rtsp.h"
#include "service_registry.h"
#include "system.h"
#include "system/network.h"
#include "system/time.h"
#include "system/upgrade.h"
#include "webrtc.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace {

#ifndef LIVE_STREAM_RELEASE_VERSION
#define LIVE_STREAM_RELEASE_VERSION "0.1.0"
#endif

constexpr const char* kDeviceName = "Binary";

std::string UptimeToString(int64_t uptime_ms) {
    if (uptime_ms <= 0) {
        return "0s";
    }
    const int64_t total_seconds = uptime_ms / 1000;
    const int64_t days = total_seconds / (24 * 60 * 60);
    const int64_t hours = (total_seconds / (60 * 60)) % 24;
    const int64_t minutes = (total_seconds / 60) % 60;
    const int64_t seconds = total_seconds % 60;
    if (days > 0) {
        return std::to_string(days) + "d " + std::to_string(hours) + "h " +
               std::to_string(minutes) + "m";
    }
    if (hours > 0) {
        return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
    }
    if (minutes > 0) {
        return std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
    }
    return std::to_string(seconds) + "s";
}

void AddModule(Json *modules, const char *name, bool running) {
    Json module = Json::object();
    module["name"] = name;
    module["state"] = running ? "running" : "pending";
    modules->push_back(module);
}

}  // namespace

Json BuildSystemOverviewJson(ISystem *system,
                             ITime *time,
                             INetwork *network,
                             IAlarm *alarm,
                             IUpgrade *upgrade,
                             IAiReader *ai,
                             DeviceMedia *device) {
    Json root = Json::object();
    DeviceInfo device_info;
    SystemInfo system_info;
    ILogger *logger = Runtime::Logger();
    IConfig *config = Runtime::Config();
    IAuth *auth = Runtime::Auth();
    IRtspSessionReader *rtsp_session_reader = ServiceRegistry::Rtsp();
    IOnvifReader *onvif_reader = ServiceRegistry::Onvif();
    IWebrtcReader *webrtc_reader = ServiceRegistry::Webrtc();
    MediaStreams *media_streams = MediaSourceRegistry::Streams();
    if (system != nullptr) {
        device_info = system->GetDeviceInfo();
        system_info = system->GetSystemInfo();
    }
    root["deviceName"] = kDeviceName;
    root["model"] = device_info.model.empty()
                        ? std::string("live_stream_ipc")
                        : device_info.model;
    root["firmware"] = device_info.firmware_version.empty()
                           ? std::string(LIVE_STREAM_RELEASE_VERSION)
                           : device_info.firmware_version;
    root["software"] = device_info.software_version.empty()
                          ? std::string(LIVE_STREAM_RELEASE_VERSION)
                          : device_info.software_version;
    root["uptime"] = UptimeToString(system_info.uptime_ms);
    root["cpu"] = system_info.cpu_usage_percent;
    root["memory"] = system_info.memory_usage_percent;
    root["temperature"] = system_info.temperature_celsius;

    Json modules = Json::array();
    AddModule(&modules, "logger", logger != nullptr && logger->IsStarted());
    AddModule(&modules, "config", config != nullptr && config->IsStarted());
    AddModule(&modules, "auth", auth != nullptr && auth->IsStarted());
    AddModule(&modules, "system",
              system != nullptr && system->IsStarted());
    AddModule(&modules, "time", time != nullptr && time->IsStarted());
    AddModule(&modules, "system.network",
              network != nullptr && network->IsStarted());
    AddModule(&modules, "alarm", alarm != nullptr && alarm->IsStarted());
    AddModule(&modules, "upgrade",
              upgrade != nullptr && upgrade->IsStarted());
    AddModule(&modules, "rtsp",
              rtsp_session_reader != nullptr &&
                  rtsp_session_reader->LocalAddress().port != 0);
    AddModule(&modules, "onvif",
              onvif_reader != nullptr && onvif_reader->IsStarted());
    AddModule(&modules, "http", true);
    AddModule(&modules, "device", device != nullptr && device->IsStarted());
    if (IsAiConfigEnabled(config)) {
        AddModule(&modules, "ai", IsAiHealthy(ai));
    }
    SnapshotInfo snapshot_info;
    if (device != nullptr) {
        snapshot_info = device->GetSnapshotInfo();
    }
    AddModule(&modules, "snapshot", device != nullptr && snapshot_info.enabled);
    bool webrtc_running = false;
    if (webrtc_reader != nullptr) {
        const WebrtcStats stats = webrtc_reader->GetStats();
        webrtc_running = stats.enabled && stats.signaling_ready;
    }
    AddModule(&modules, "webrtc", webrtc_running);
    bool media_running = false;
    if (media_streams != nullptr) {
        media_running = media_streams->GetStreamStats().enabled;
    }
    AddModule(&modules, "media", media_running);
    root["modules"] = modules;
    return root;
}

}  // namespace live_stream
