#include "handlers/http_handlers.h"

#include "http_ai_status.h"
#include "http_auth_gate.h"
#include "http_response.h"

#include "alarm.h"
#include "config.h"
#include "device.h"
#include "system/network.h"
#include "onvif_server.h"
#include "rtsp.h"
#include "system.h"
#include "system/time.h"
#include "system/upgrade.h"
#include "webrtc.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace {

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

Json SystemCapabilitiesToJson(const SystemCapabilities &capabilities) {
    Json root = Json::object();
    root["supports_reboot"] = capabilities.supports_reboot;
    root["supports_factory_reset"] = capabilities.supports_factory_reset;
    Json features = Json::array();
    for (const std::string &feature : capabilities.features) {
        features.push_back(feature);
    }
    root["features"] = features;
    return root;
}

}  // namespace

class SystemHttpHandler : public IHttpHandler {
public:
    explicit SystemHttpHandler(const SystemHandlerDependencies &dependencies)
        : access_(dependencies.access),
          system_(dependencies.system),
          overview_(dependencies.overview) {}

    void RegisterRoutes(IHttpRouter &router) override {
        router.AddExactRoute(HttpMethod::kGet, "/api/system/status",
                             this, &SystemHttpHandler::HandleOverview);
        if (system_ == nullptr) {
            return;
        }
        router.AddExactRoute(HttpMethod::kGet, "/api/system/capabilities",
                             this, &SystemHttpHandler::HandleCapabilities);
        router.AddExactRoute(HttpMethod::kPost, "/api/system/reboot",
                             this, &SystemHttpHandler::HandleReboot);
        router.AddExactRoute(HttpMethod::kPost,
                             "/api/system/factory-reset", this,
                             &SystemHttpHandler::HandleFactoryReset);
    }

private:
    HttpResponse HandleOverview(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response =
            RequireAuthResponse(access_, request, &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }

        Json root = Json::object();
        DeviceInfo device_info;
        SystemInfo system_info;
        if (system_ != nullptr) {
            device_info = system_->GetDeviceInfo();
            system_info = system_->GetSystemInfo();
        }
        root["deviceName"] = device_info.serial_number.empty()
                                 ? std::string("live-stream-ipc")
                                 : device_info.serial_number;
        root["model"] = device_info.model.empty()
                            ? std::string("live_stream_ipc")
                            : device_info.model;
        root["firmware"] = device_info.firmware_version.empty()
                               ? std::string("0.1.0")
                               : device_info.firmware_version;
        root["uptime"] = UptimeToString(system_info.uptime_ms);
        root["cpu"] = system_info.cpu_usage_percent;
        root["memory"] = system_info.memory_usage_percent;
        root["temperature"] = system_info.temperature_celsius;
        Json modules = Json::array();
        auto add_module = [&modules](const char *name, bool running) {
            Json module = Json::object();
            module["name"] = name;
            module["state"] = running ? "running" : "pending";
            modules.push_back(module);
        };
        add_module("logger",
                   overview_.logger != nullptr &&
                       overview_.logger->IsStarted());
        add_module("config",
                   overview_.config != nullptr &&
                       overview_.config->IsStarted());
        add_module("auth",
                   overview_.auth != nullptr &&
                       overview_.auth->IsStarted());
        add_module("system",
                   system_ != nullptr &&
                       system_->IsStarted());
        add_module("time",
                   overview_.time != nullptr &&
                       overview_.time->IsStarted());
        add_module("system.network",
                   overview_.network != nullptr &&
                       overview_.network->IsStarted());
        add_module("alarm",
                   overview_.alarm != nullptr &&
                       overview_.alarm->IsStarted());
        add_module("upgrade",
                   overview_.upgrade != nullptr &&
                       overview_.upgrade->IsStarted());
        add_module("rtsp",
                   overview_.rtsp_session_reader != nullptr &&
                       overview_.rtsp_session_reader->LocalAddress().port != 0);
        add_module("onvif",
                   overview_.onvif_status_reader != nullptr &&
                       overview_.onvif_status_reader->IsStarted());
        add_module("http", true);
        add_module("device",
                   overview_.device != nullptr &&
                       overview_.device->IsStarted());
        if (IsAiConfigEnabled(overview_.config)) {
            add_module("ai", IsAiHealthy(overview_.ai));
        }
        SnapshotInfo snapshot_info;
        if (overview_.device != nullptr) {
            snapshot_info = overview_.device->GetSnapshotInfo();
        }
        add_module("snapshot",
                   overview_.device != nullptr &&
                       snapshot_info.enabled);
        bool webrtc_running = false;
        if (overview_.webrtc_status_reader != nullptr) {
            const WebrtcStats stats =
                overview_.webrtc_status_reader->GetStats();
            webrtc_running = stats.enabled && stats.signaling_ready;
        }
        add_module("webrtc", webrtc_running);
        bool media_running = false;
        if (overview_.media_streams != nullptr) {
            media_running =
                overview_.media_streams->GetStreamStats().enabled;
        }
        add_module("media", media_running);
        root["modules"] = modules;
        return JsonResponse(200, root);
    }

    HttpResponse HandleCapabilities(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kReadStatus, "system",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        return JsonResponse(
            200, SystemCapabilitiesToJson(
                     system_->GetCapabilities()));
    }

    HttpResponse HandleReboot(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kReboot, "system", &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        const SystemCapabilities capabilities =
            system_->GetCapabilities();
        if (!capabilities.supports_reboot) {
            return StatusResponse(501, "Reboot not supported");
        }
        return system_->Reboot(
                   access_->MakeContext(request, &principal))
                   ? OkResponse()
                   : StatusResponse(503, "Reboot failed");
    }

    HttpResponse HandleFactoryReset(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kFactoryReset, "system",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        const SystemCapabilities capabilities =
            system_->GetCapabilities();
        if (!capabilities.supports_factory_reset) {
            return StatusResponse(501, "Factory reset not supported");
        }
        return system_->FactoryReset(
                   access_->MakeContext(request, &principal))
                   ? OkResponse()
                   : StatusResponse(503, "Factory reset failed");
    }

    HttpAccess *access_ = nullptr;
    ISystem *system_ = nullptr;
    SystemOverviewSources overview_;
};

std::unique_ptr<IHttpHandler> MakeSystemHandler(
    const SystemHandlerDependencies &dependencies) {
    return std::unique_ptr<IHttpHandler>(
        new SystemHttpHandler(dependencies));
}

}  // namespace live_stream
