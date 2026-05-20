#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "media_service.h"
#include "rtsp_service.h"
#include "snapshot_service.h"
#include "stream_hub_service.h"
#include "system_service.h"
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

HttpResponse http_handlers::HandleSystemStatus(HttpHandlerContext *context, const HttpRequest &request) {
    AuthPrincipal principal = context->Authenticate(request);
    if (principal.user_name.empty()) {
        return StatusResponse(401, "Unauthorized");
    }

    ConfigJson root = ConfigJson::object();
    DeviceInfo device_info;
    SystemStatus system_status;
    if (context->Dependencies().system_service != nullptr) {
        device_info = context->Dependencies().system_service->GetDeviceInfo();
        system_status = context->Dependencies().system_service->GetSystemStatus();
    }
    root["deviceName"] = device_info.serial_number.empty()
                             ? std::string("live-stream-ipc")
                             : device_info.serial_number;
    root["model"] = device_info.model.empty() ? std::string("live_stream_ipc")
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
    add_service("logger_service", context->Dependencies().logger_service != nullptr);
    add_service("config_service", context->Dependencies().config_service != nullptr);
    add_service("auth_service", context->Dependencies().auth_service != nullptr);
    add_service("system_service", context->Dependencies().system_service != nullptr);
    add_service("time_service", context->Dependencies().time_service != nullptr);
    add_service("network_service", context->Dependencies().network_service != nullptr);
    add_service("alarm_service", context->Dependencies().alarm_service != nullptr);
    add_service("upgrade_service", context->Dependencies().upgrade_service != nullptr);
    add_service("rtsp_service",
                context->Dependencies().rtsp_service != nullptr &&
                    context->Dependencies().rtsp_service->LocalAddress().port != 0);
    add_service("onvif_service", context->Dependencies().onvif_service != nullptr);
    add_service("http_service", true);
    add_service("media_service", context->Dependencies().media_service != nullptr &&
                                     context->Dependencies().media_service->IsStarted());
    if (IsAiConfigEnabled(context->Dependencies().config_service)) {
        add_service("ai_service", IsAiServiceHealthy(context->Dependencies().ai_service));
    }
    add_service("snapshot_service",
                context->Dependencies().snapshot_service != nullptr &&
                    context->Dependencies().snapshot_service->GetStats().enabled);
    add_service("webrtc_service",
                context->Dependencies().webrtc_service != nullptr &&
                    context->Dependencies().webrtc_service->GetStats().enabled &&
                    context->Dependencies().webrtc_service->GetStats().backend_available);
    add_service("stream_hub_service",
                context->Dependencies().stream_hub_service != nullptr &&
                    context->Dependencies().stream_hub_service->GetStats().enabled);
    root["services"] = services;
    return JsonResponse(200, root);
}

HttpResponse http_handlers::HandleSystemCapabilities(HttpHandlerContext *context, const HttpRequest &request) {
    if (context->Dependencies().system_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!context->RequirePermission(request, AuthPermission::kReadStatus, "system",
                                    &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    return JsonResponse(200,
                        SystemCapabilitiesToJson(
                            context->Dependencies().system_service->GetCapabilities()));
}

HttpResponse http_handlers::HandleSystemReboot(HttpHandlerContext *context, const HttpRequest &request) {
    if (context->Dependencies().system_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!context->RequirePermission(request, AuthPermission::kReboot, "system",
                                    &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    const SystemCapabilities capabilities =
        context->Dependencies().system_service->GetCapabilities();
    if (!capabilities.supports_reboot) {
        return StatusResponse(501, "Reboot not supported");
    }
    return context->Dependencies().system_service->Reboot(
               context->MakeContext(request, &principal))
               ? OkResponse()
               : StatusResponse(503, "Reboot failed");
}

HttpResponse http_handlers::HandleSystemFactoryReset(HttpHandlerContext *context, const HttpRequest &request) {
    if (context->Dependencies().system_service == nullptr) {
        return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!context->RequirePermission(request, AuthPermission::kFactoryReset, "system",
                                    &principal)) {
        return StatusResponse(403, "Forbidden");
    }
    const SystemCapabilities capabilities =
        context->Dependencies().system_service->GetCapabilities();
    if (!capabilities.supports_factory_reset) {
        return StatusResponse(501, "Factory reset not supported");
    }
    return context->Dependencies().system_service->FactoryReset(
               context->MakeContext(request, &principal))
               ? OkResponse()
               : StatusResponse(503, "Factory reset failed");
}

}  // namespace live_stream
