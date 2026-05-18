#include "runtime_config.h"

#include "live_stream/json_utils.h"

namespace live_stream {
namespace {

bool ParseRtspVideoCodec(const std::string &value, VideoCodec *codec) {
    if (codec == nullptr) {
        return false;
    }
    if (value == "h265") {
        *codec = VideoCodec::kH265;
        return true;
    }
    if (value == "h264") {
        *codec = VideoCodec::kH264;
        return true;
    }
    return false;
}

bool NormalizePath(std::string *path) {
    if (path == nullptr || path->empty()) {
        return false;
    }
    if ((*path)[0] != '/') {
        path->insert(path->begin(), '/');
    }
    return true;
}

bool ApplyRtspVideoCodecConfig(const ConfigJson &video,
                               AppRuntimeConfig *config) {
    if (config == nullptr || !video.is_object()) {
        return false;
    }
    const ConfigJson *streams = nullptr;
    if (!json_utils::LoadObject(video, "streams", &streams)) {
        return false;
    }
    const ConfigJson *main = nullptr;
    const ConfigJson *sub = nullptr;
    if (!json_utils::LoadObject(*streams, "main", &main) ||
        !json_utils::LoadObject(*streams, "sub", &sub)) {
        return false;
    }
    std::string codec;
    if (!json_utils::Load(*main, "codec", &codec) ||
        !ParseRtspVideoCodec(codec, &config->rtsp_main_codec) ||
        !json_utils::Load(*sub, "codec", &codec) ||
        !ParseRtspVideoCodec(codec, &config->rtsp_sub_codec)) {
        return false;
    }
    return true;
}

bool ApplyNetworkConfig(const ConfigJson &network, AppRuntimeConfig *config) {
    if (config == nullptr || !network.is_object()) {
        return false;
    }
    const ConfigJson *ports = nullptr;
    if (!json_utils::Load(network, "advertise_ip", &config->advertise_host) ||
        config->advertise_host.empty() ||
        !json_utils::LoadObject(network, "ports", &ports)) {
        return false;
    }
    // default_ifname is optional; falls back to the struct default ("eth0").
    std::string ifname;
    if (json_utils::Load(network, "default_ifname", &ifname) && !ifname.empty()) {
        config->network_ifname = ifname;
    }
    return json_utils::Load(*ports, "http", &config->http_port, 1, 65535) &&
           json_utils::Load(*ports, "rtsp", &config->rtsp_port, 1, 65535) &&
           json_utils::Load(*ports, "onvif", &config->onvif_device_port, 1,
                            65535);
}

bool ApplyHttpConfig(const ConfigJson &http, AppRuntimeConfig *config) {
    if (config == nullptr || !http.is_object()) {
        return false;
    }
    return json_utils::Load(http, "port", &config->http_port, 1, 65535) &&
           json_utils::Load(http, "static_root", &config->static_root) &&
           !config->static_root.empty();
}

bool ApplyRtspConfig(const ConfigJson &rtsp, AppRuntimeConfig *config) {
    if (config == nullptr || !rtsp.is_object()) {
        return false;
    }
    return json_utils::Load(rtsp, "port", &config->rtsp_port, 1, 65535) &&
           json_utils::Load(rtsp, "auth_required", &config->rtsp_auth_required) &&
           json_utils::Load(rtsp, "max_sessions", &config->rtsp_max_sessions, 1,
                            0xffffffffU);
}

bool ApplySnapshotConfig(const ConfigJson &snapshot, AppRuntimeConfig *config) {
    if (config == nullptr || !snapshot.is_object()) {
        return false;
    }
    if (!json_utils::Load(snapshot, "main_path", &config->snapshot_main_path) ||
        !NormalizePath(&config->snapshot_main_path) ||
        !json_utils::Load(snapshot, "sub_path", &config->snapshot_sub_path) ||
        !NormalizePath(&config->snapshot_sub_path)) {
        return false;
    }
    return true;
}

bool ApplyWebrtcConfig(const ConfigJson &webrtc, AppRuntimeConfig *config) {
    if (config == nullptr || !webrtc.is_object()) {
        return false;
    }
    if (!json_utils::Load(webrtc, "enabled", &config->webrtc_enabled) ||
        !json_utils::Load(webrtc, "prefer_tcp", &config->webrtc_prefer_tcp) ||
        !json_utils::Load(webrtc, "local_port_base",
                          &config->webrtc_local_port_base, 1, 65535) ||
        !json_utils::Load(webrtc, "max_peers", &config->webrtc_max_peers, 1,
                          0xffffffffU) ||
        !json_utils::Load(webrtc, "public_ip", &config->advertise_host) ||
        config->advertise_host.empty()) {
        return false;
    }
    const ConfigJson *ice_servers = nullptr;
    if (!json_utils::LoadArray(webrtc, "ice_servers", &ice_servers)) {
        return false;
    }
    config->webrtc_ice_servers.clear();
    for (const ConfigJson &item : *ice_servers) {
        if (!item.is_object()) {
            return false;
        }
        WebrtcIceServer server;
        if (!json_utils::Load(item, "url", &server.url) ||
            !json_utils::Load(item, "username", &server.username) ||
            !json_utils::Load(item, "credential", &server.credential)) {
            return false;
        }
        config->webrtc_ice_servers.push_back(server);
    }
    return true;
}

bool ApplyOnvifConfig(const ConfigJson &onvif, AppRuntimeConfig *config) {
    if (config == nullptr || !onvif.is_object()) {
        return false;
    }
    return json_utils::Load(onvif, "device_service_port",
                            &config->onvif_device_port, 1, 65535) &&
           json_utils::Load(onvif, "discovery_port",
                            &config->onvif_discovery_port, 1, 65535) &&
           json_utils::Load(onvif, "discovery_enabled",
                            &config->onvif_discovery_enabled) &&
           json_utils::Load(onvif, "auth_required",
                            &config->onvif_auth_required) &&
           json_utils::Load(onvif, "advertise_ip", &config->advertise_host) &&
           !config->advertise_host.empty() &&
           json_utils::Load(onvif, "manufacturer", &config->onvif_manufacturer) &&
           json_utils::Load(onvif, "model", &config->onvif_model) &&
           json_utils::Load(onvif, "firmware_version",
                            &config->onvif_firmware_version);
}

}  // namespace

bool LoadRuntimeConfig(IConfigService *config_service,
                       AppRuntimeConfig *config) {
    if (config_service == nullptr || config == nullptr) {
        return false;
    }
    AppRuntimeConfig runtime;
    if (!ApplyRtspVideoCodecConfig(config_service->GetValue("video"), &runtime) ||
        !ApplyNetworkConfig(config_service->GetValue("network"), &runtime) ||
        !ApplyHttpConfig(config_service->GetValue("http"), &runtime) ||
        !ApplyRtspConfig(config_service->GetValue("rtsp"), &runtime) ||
        !ApplySnapshotConfig(config_service->GetValue("snapshot"), &runtime) ||
        !ApplyWebrtcConfig(config_service->GetValue("webrtc"), &runtime) ||
        !ApplyOnvifConfig(config_service->GetValue("onvif"), &runtime)) {
        return false;
    }
    *config = runtime;
    return true;
}

}  // namespace live_stream
