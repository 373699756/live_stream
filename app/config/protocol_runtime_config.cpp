#include "config/protocol_runtime_config.h"

#include <cstddef>
#include <vector>

namespace live_stream {
namespace {

ConfigResult RejectRuntimeConfigChange(const char *field) {
    return ConfigResult::Failure(field == nullptr ? "" : field,
                                 "restart required");
}

bool SameIceServers(const std::vector<WebrtcIceServer> &left,
                    const std::vector<WebrtcIceServer> &right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].url != right[i].url ||
            left[i].username != right[i].username ||
            left[i].credential != right[i].credential) {
            return false;
        }
    }
    return true;
}

}  // namespace

ConfigResult ValidateRuntimeConfigScope(
    const AppRuntimeConfig &current_config,
    const AppRuntimeConfig &next_config,
    const std::string &scope) {
    if (scope == "http") {
        if (next_config.http_port != current_config.http_port) {
            return RejectRuntimeConfigChange("port");
        }
        if (next_config.static_root != current_config.static_root) {
            return RejectRuntimeConfigChange("static_root");
        }
        return ConfigResult::Success();
    }
    if (scope == "rtsp") {
        if (next_config.rtsp_port != current_config.rtsp_port) {
            return RejectRuntimeConfigChange("port");
        }
        if (next_config.rtsp_max_sessions !=
            current_config.rtsp_max_sessions) {
            return RejectRuntimeConfigChange("max_sessions");
        }
        return ConfigResult::Success();
    }
    if (scope == "webrtc") {
        if (next_config.webrtc_local_port_base !=
            current_config.webrtc_local_port_base) {
            return RejectRuntimeConfigChange("local_port_base");
        }
        return ConfigResult::Success();
    }
    if (scope == "onvif") {
        if (next_config.onvif_device_port !=
            current_config.onvif_device_port) {
            return RejectRuntimeConfigChange("device_service_port");
        }
        if (next_config.onvif_discovery_port !=
            current_config.onvif_discovery_port) {
            return RejectRuntimeConfigChange("discovery_port");
        }
        if (next_config.onvif_discovery_enabled !=
            current_config.onvif_discovery_enabled) {
            return RejectRuntimeConfigChange("discovery_enabled");
        }
        return ConfigResult::Success();
    }
    return ConfigResult::Failure("", "unsupported runtime config scope");
}

bool IsRtspRuntimeChanged(const AppRuntimeConfig &current_config,
                          const AppRuntimeConfig &next_config) {
    return current_config.rtsp_auth_required !=
               next_config.rtsp_auth_required ||
           current_config.rtsp_main_codec != next_config.rtsp_main_codec ||
           current_config.rtsp_sub_codec != next_config.rtsp_sub_codec;
}

bool IsWebrtcRuntimeChanged(const AppRuntimeConfig &current_config,
                            const AppRuntimeConfig &next_config) {
    return current_config.webrtc_enabled != next_config.webrtc_enabled ||
           current_config.webrtc_prefer_tcp != next_config.webrtc_prefer_tcp ||
           current_config.webrtc_max_peers != next_config.webrtc_max_peers ||
           current_config.webrtc_public_ip != next_config.webrtc_public_ip ||
           current_config.network_ifname != next_config.network_ifname ||
           current_config.advertise_host != next_config.advertise_host ||
           !SameIceServers(current_config.webrtc_ice_servers,
                           next_config.webrtc_ice_servers);
}

bool IsOnvifRuntimeChanged(const AppRuntimeConfig &current_config,
                           const AppRuntimeConfig &next_config) {
    return current_config.advertise_host != next_config.advertise_host ||
           current_config.onvif_auth_required !=
               next_config.onvif_auth_required ||
           current_config.onvif_manufacturer !=
               next_config.onvif_manufacturer ||
           current_config.onvif_model != next_config.onvif_model ||
           current_config.onvif_firmware_version !=
               next_config.onvif_firmware_version ||
           current_config.http_port != next_config.http_port;
}

}  // namespace live_stream
