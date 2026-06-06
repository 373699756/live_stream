#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "alarm_service.h"
#include "config_service.h"
#include "media_service.h"
#include "network_service.h"
#include "onvif_service.h"
#include "rtsp_service.h"
#include "snapshot_service.h"
#include "media_source.h"
#include "system_service.h"
#include "time_service.h"
#include "upgrade_service.h"
#include "webrtc_service.h"

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
                      ISystemService *system_service,
                      const SystemStatusSources &status_sources)
        : access_(access), system_service_(system_service),
          status_sources_(status_sources) {}

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
        if (system_service_ != nullptr) {
            device_info = system_service_->GetDeviceInfo();
            system_status = system_service_->GetSystemStatus();
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
        ConfigJson services = ConfigJson::array();
        auto add_service = [&services](const char *name, bool running) {
            ConfigJson service = ConfigJson::object();
            service["name"] = name;
            service["state"] = running ? "running" : "pending";
            services.push_back(service);
        };
        add_service("logger_service",
                    status_sources_.logger_service != nullptr &&
                        status_sources_.logger_service->IsStarted());
        add_service("config_service",
                    status_sources_.config_service != nullptr &&
                        status_sources_.config_service->IsStarted());
        add_service("auth_service",
                    status_sources_.auth_service != nullptr &&
                        status_sources_.auth_service->IsStarted());
        add_service("system_service",
                    system_service_ != nullptr &&
                        system_service_->IsStarted());
        add_service("time_service",
                    status_sources_.time_service != nullptr &&
                        status_sources_.time_service->IsStarted());
        add_service("network_service",
                    status_sources_.network_service != nullptr &&
                        status_sources_.network_service->IsStarted());
        add_service("alarm_service",
                    status_sources_.alarm_service != nullptr &&
                        status_sources_.alarm_service->IsStarted());
        add_service("upgrade_service",
                    status_sources_.upgrade_service != nullptr &&
                        status_sources_.upgrade_service->IsStarted());
        add_service("rtsp_service",
                    status_sources_.rtsp_service != nullptr &&
                        status_sources_.rtsp_service->LocalAddress().port != 0);
        add_service("onvif_service",
                    status_sources_.onvif_service != nullptr &&
                        status_sources_.onvif_service->IsStarted());
        add_service("http_service", true);
        add_service("media_service",
                    status_sources_.media_service != nullptr &&
                        status_sources_.media_service->IsStarted());
        if (IsAiConfigEnabled(status_sources_.config_service)) {
            add_service("ai_service",
                        IsAiServiceHealthy(status_sources_.ai_service));
        }
        add_service("snapshot_service",
                    status_sources_.snapshot_service != nullptr &&
                        status_sources_.snapshot_service->GetStats().enabled);
        bool webrtc_running = false;
        if (status_sources_.webrtc_service != nullptr) {
            const WebrtcServiceStats stats =
                status_sources_.webrtc_service->GetStats();
            webrtc_running = stats.enabled && stats.backend_available;
        }
        add_service("webrtc_service", webrtc_running);
        add_service("media_source",
                    status_sources_.media_source != nullptr &&
                        status_sources_.media_source->GetStats().enabled);
        root["services"] = services;
        return JsonResponse(200, root);
    }

    HttpResponse HandleCapabilities(const HttpRequest &request) {
        if (system_service_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!access_->RequirePermission(request, AuthPermission::kReadStatus,
                                         "system", &principal)) {
            return ForbiddenResponse(principal);
        }
        return JsonResponse(
            200, SystemCapabilitiesToJson(
                     system_service_->GetCapabilities()));
    }

    HttpResponse HandleReboot(const HttpRequest &request) {
        if (system_service_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!access_->RequirePermission(request, AuthPermission::kReboot,
                                         "system", &principal)) {
            return ForbiddenResponse(principal);
        }
        const SystemCapabilities capabilities =
            system_service_->GetCapabilities();
        if (!capabilities.supports_reboot) {
            return StatusResponse(501, "Reboot not supported");
        }
        return system_service_->Reboot(
                   access_->MakeContext(request, &principal))
                   ? OkResponse()
                   : StatusResponse(503, "Reboot failed");
    }

    HttpResponse HandleFactoryReset(const HttpRequest &request) {
        if (system_service_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!access_->RequirePermission(request,
                                         AuthPermission::kFactoryReset,
                                         "system", &principal)) {
            return ForbiddenResponse(principal);
        }
        const SystemCapabilities capabilities =
            system_service_->GetCapabilities();
        if (!capabilities.supports_factory_reset) {
            return StatusResponse(501, "Factory reset not supported");
        }
        return system_service_->FactoryReset(
                   access_->MakeContext(request, &principal))
                   ? OkResponse()
                   : StatusResponse(503, "Factory reset failed");
    }

    HttpAccess *access_ = nullptr;
    ISystemService *system_service_ = nullptr;
    SystemStatusSources status_sources_;
};

std::unique_ptr<IHttpHandler> CreateSystemHttpHandler(
    HttpAccess *access, ISystemService *system_service,
    const SystemStatusSources &status_sources) {
    return std::unique_ptr<IHttpHandler>(
        new SystemHttpHandler(access, system_service, status_sources));
}

}  // namespace live_stream
