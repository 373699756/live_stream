#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "alarm.h"
#include "config.h"
#include "device.h"
#include "network_api.h"
#include "onvif_server.h"
#include "rtsp.h"
#include "system.h"
#include "time_api.h"
#include "upgrade.h"
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

ConfigJson SystemCapabilitiesToJson(const SystemCapabilities &capabilities) {
    ConfigJson root = ConfigJson::object();
    root["supports_reboot"] = capabilities.supports_reboot;
    root["supports_factory_reset"] = capabilities.supports_factory_reset;
    ConfigJson features = ConfigJson::array();
    for (const std::string &feature : capabilities.features) {
        features.push_back(feature);
    }
    root["features"] = features;
    return root;
}

}  // namespace

class SystemHttpHandler : public IHttpHandler {
public:
    SystemHttpHandler(HttpAccess *access,
                      ISystem *system,
                      const SystemStatusSources &status_sources)
        : access_(access), system_(system), status_sources_(status_sources) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kGet, "/api/system/status",
                              &SystemHttpHandler::HandleStatusRoute, this);
        router->AddExactRoute(HttpMethod::kGet, "/api/system/capabilities",
                              &SystemHttpHandler::HandleCapabilitiesRoute,
                              this);
        router->AddExactRoute(HttpMethod::kPost, "/api/system/reboot",
                              &SystemHttpHandler::HandleRebootRoute, this);
        router->AddExactRoute(HttpMethod::kPost,
                              "/api/system/factory-reset",
                              &SystemHttpHandler::HandleFactoryResetRoute,
                              this);
    }

private:
    static HttpResponse HandleStatusRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<SystemHttpHandler *>(user)->HandleStatus(request);
    }

    static HttpResponse HandleCapabilitiesRoute(void *user,
                                                const HttpRequest &request) {
        return static_cast<SystemHttpHandler *>(user)->HandleCapabilities(
            request);
    }

    static HttpResponse HandleRebootRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<SystemHttpHandler *>(user)->HandleReboot(request);
    }

    static HttpResponse HandleFactoryResetRoute(void *user,
                                                const HttpRequest &request) {
        return static_cast<SystemHttpHandler *>(user)->HandleFactoryReset(
            request);
    }

    HttpResponse HandleStatus(const HttpRequest &request) {
        AuthPrincipal principal = access_->Authenticate(request);
        if (principal.user_name.empty()) {
            return StatusResponse(401, "Unauthorized");
        }
        if (principal.must_change_password) {
            return ForbiddenResponse(principal);
        }

        ConfigJson root = ConfigJson::object();
        DeviceInfo device_info;
        SystemStatus system_status;
        if (system_ != nullptr) {
            device_info = system_->GetDeviceInfo();
            system_status = system_->GetSystemStatus();
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
        root["uptime"] = UptimeToString(system_status.uptime_ms);
        root["cpu"] = system_status.cpu_usage_percent;
        root["memory"] = system_status.memory_usage_percent;
        root["temperature"] = system_status.temperature_celsius;
        ConfigJson modules = ConfigJson::array();
        auto add_module = [&modules](const char *name, bool running) {
            ConfigJson module = ConfigJson::object();
            module["name"] = name;
            module["state"] = running ? "running" : "pending";
            modules.push_back(module);
        };
        add_module("logger",
                   status_sources_.logger != nullptr &&
                       status_sources_.logger->IsStarted());
        add_module("config",
                   status_sources_.config != nullptr &&
                       status_sources_.config->IsStarted());
        add_module("auth",
                   status_sources_.auth != nullptr &&
                       status_sources_.auth->IsStarted());
        add_module("system",
                   system_ != nullptr &&
                       system_->IsStarted());
        add_module("time",
                   status_sources_.time != nullptr &&
                       status_sources_.time->IsStarted());
        add_module("system.network",
                   status_sources_.network != nullptr &&
                       status_sources_.network->IsStarted());
        add_module("alarm",
                   status_sources_.alarm != nullptr &&
                       status_sources_.alarm->IsStarted());
        add_module("upgrade",
                   status_sources_.upgrade != nullptr &&
                       status_sources_.upgrade->IsStarted());
        add_module("rtsp",
                   status_sources_.rtsp != nullptr &&
                       status_sources_.rtsp->LocalAddress().port != 0);
        add_module("onvif",
                   status_sources_.onvif != nullptr &&
                       status_sources_.onvif->IsStarted());
        add_module("http", true);
        add_module("device",
                   status_sources_.device != nullptr &&
                       status_sources_.device->IsStarted());
        if (IsAiConfigEnabled(status_sources_.config)) {
            add_module("ai", IsAiHealthy(status_sources_.ai));
        }
        SnapshotInfo snapshot_info;
        if (status_sources_.device != nullptr) {
            snapshot_info = status_sources_.device->GetSnapshotInfo();
        }
        add_module("snapshot",
                   status_sources_.device != nullptr &&
                       snapshot_info.enabled);
        bool webrtc_running = false;
        if (status_sources_.webrtc != nullptr) {
            const WebrtcStats stats =
                status_sources_.webrtc->GetStats();
            webrtc_running = stats.enabled && stats.signaling_ready;
        }
        add_module("webrtc", webrtc_running);
        bool media_running = false;
        if (status_sources_.media_streams != nullptr) {
            media_running =
                status_sources_.media_streams->GetStreamStats().enabled;
        }
        add_module("media", media_running);
        root["modules"] = modules;
        return JsonResponse(200, root);
    }

    HttpResponse HandleCapabilities(const HttpRequest &request) {
        if (system_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!access_->RequirePermission(request, AuthPermission::kReadStatus,
                                        "system", &principal)) {
            return ForbiddenResponse(principal);
        }
        return JsonResponse(
            200, SystemCapabilitiesToJson(
                     system_->GetCapabilities()));
    }

    HttpResponse HandleReboot(const HttpRequest &request) {
        if (system_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!access_->RequirePermission(request, AuthPermission::kReboot,
                                        "system", &principal)) {
            return ForbiddenResponse(principal);
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
        if (system_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!access_->RequirePermission(request,
                                        AuthPermission::kFactoryReset,
                                        "system", &principal)) {
            return ForbiddenResponse(principal);
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
    SystemStatusSources status_sources_;
};

std::unique_ptr<IHttpHandler> MakeSystemHandler(
    HttpAccess *access, ISystem *system,
    const SystemStatusSources &status_sources) {
    return std::unique_ptr<IHttpHandler>(
        new SystemHttpHandler(access, system, status_sources));
}

}  // namespace live_stream
