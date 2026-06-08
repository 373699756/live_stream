#include "config/runtime_config.h"

#include "json_utils.h"

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

bool ApplyRtspVideoCodecConfig(const ConfigJson &video,
                               AppRuntimeConfig *config) {
    if (config == nullptr || !video.is_object()) {
        return false;
    }
    if (!video.contains("streams") || !video.at("streams").is_object()) {
        return false;
    }
    const ConfigJson &streams = video.at("streams");
    if (!streams.contains("main") || !streams.at("main").is_object() ||
        !streams.contains("sub") || !streams.at("sub").is_object()) {
        return false;
    }
    const ConfigJson &main = streams.at("main");
    const ConfigJson &sub = streams.at("sub");
    std::string codec;
    if (!json_utils::ReadField(main, "codec", &codec)) {
        return false;
    }
    if (!ParseRtspVideoCodec(codec, &config->rtsp_main_codec)) {
        return false;
    }
    if (!json_utils::ReadField(sub, "codec", &codec)) {
        return false;
    }
    if (!ParseRtspVideoCodec(codec, &config->rtsp_sub_codec)) {
        return false;
    }
    return true;
}

bool ApplyNetworkConfig(const ConfigJson &network, AppRuntimeConfig *config) {
    if (config == nullptr || !network.is_object()) {
        return false;
    }
    if (!json_utils::ReadField(network, "advertise_ip",
                               &config->advertise_host)) {
        return false;
    }
    if (config->advertise_host.empty()) {
        return false;
    }
    // default_ifname is optional; falls back to the struct default ("eth0").
    std::string ifname;
    if (json_utils::ReadField(network, "default_ifname", &ifname)) {
        if (!ifname.empty()) {
            config->network_ifname = ifname;
        }
    }
    return true;
}

bool ApplyHttpConfig(const ConfigJson &http, AppRuntimeConfig *config) {
    if (config == nullptr || !http.is_object()) {
        return false;
    }
    if (!json_utils::ReadField(http, "port", &config->http_port, 1,
                               65535)) {
        return false;
    }
    if (!json_utils::ReadField(http, "static_root", &config->static_root)) {
        return false;
    }
    return !config->static_root.empty();
}

bool ApplyRtspConfig(const ConfigJson &rtsp, AppRuntimeConfig *config) {
    if (config == nullptr || !rtsp.is_object()) {
        return false;
    }
    if (!json_utils::ReadField(rtsp, "port", &config->rtsp_port, 1,
                               65535)) {
        return false;
    }
    if (!json_utils::ReadField(rtsp, "auth_required",
                               &config->rtsp_auth_required)) {
        return false;
    }
    if (!json_utils::ReadField(rtsp, "max_sessions",
                               &config->rtsp_max_sessions, 1, 0xffffffffU)) {
        return false;
    }
    return true;
}

bool ApplySnapshotConfig(const ConfigJson &snapshot, AppRuntimeConfig *config) {
    if (config == nullptr || !snapshot.is_object()) {
        return false;
    }
    bool enabled = true;
    uint32_t jpeg_quality = 0;
    uint32_t timeout_ms = 0;
    if (!json_utils::ReadField(snapshot, "enabled", &enabled)) {
        return false;
    }
    if (!json_utils::ReadField(snapshot, "jpeg_quality", &jpeg_quality, 1,
                               100)) {
        return false;
    }
    if (!json_utils::ReadField(snapshot, "timeout_ms", &timeout_ms, 1,
                               0xffffffffU)) {
        return false;
    }
    return true;
}

bool ApplyWebrtcConfig(const ConfigJson &webrtc, AppRuntimeConfig *config) {
    if (config == nullptr || !webrtc.is_object()) {
        return false;
    }
    std::string public_ip = config->webrtc_public_ip;
    if (!json_utils::ReadField(webrtc, "enabled",
                               &config->webrtc_enabled)) {
        return false;
    }
    if (!json_utils::ReadField(webrtc, "prefer_tcp",
                               &config->webrtc_prefer_tcp)) {
        return false;
    }
    if (!json_utils::ReadField(webrtc, "local_port_base",
                               &config->webrtc_local_port_base, 1, 65535)) {
        return false;
    }
    if (!json_utils::ReadField(webrtc, "max_peers",
                               &config->webrtc_max_peers, 1, 0xffffffffU)) {
        return false;
    }
    if (webrtc.contains("public_ip") &&
        !json_utils::ReadField(webrtc, "public_ip", &public_ip)) {
        return false;
    }
    config->webrtc_public_ip = public_ip;
    if (!webrtc.contains("ice_servers") ||
        !webrtc.at("ice_servers").is_array()) {
        return false;
    }
    const ConfigJson &ice_servers = webrtc.at("ice_servers");
    config->webrtc_ice_servers.clear();
    for (const ConfigJson &item : ice_servers) {
        if (!item.is_object()) {
            return false;
        }
        WebrtcIceServer server;
        if (!json_utils::ReadField(item, "url", &server.url)) {
            return false;
        }
        if (!json_utils::ReadField(item, "username", &server.username)) {
            return false;
        }
        if (!json_utils::ReadField(item, "credential", &server.credential)) {
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
    std::string advertise_ip;
    if (!json_utils::ReadField(onvif, "device_service_port",
                               &config->onvif_device_port, 1, 65535)) {
        return false;
    }
    if (!json_utils::ReadField(onvif, "discovery_port",
                               &config->onvif_discovery_port, 1, 65535)) {
        return false;
    }
    if (!json_utils::ReadField(onvif, "discovery_enabled",
                               &config->onvif_discovery_enabled)) {
        return false;
    }
    if (!json_utils::ReadField(onvif, "auth_required",
                               &config->onvif_auth_required)) {
        return false;
    }
    if (!json_utils::ReadField(onvif, "advertise_ip", &advertise_ip) ||
        advertise_ip.empty()) {
        return false;
    }
    if (!json_utils::ReadField(onvif, "manufacturer",
                               &config->onvif_manufacturer)) {
        return false;
    }
    if (!json_utils::ReadField(onvif, "model", &config->onvif_model)) {
        return false;
    }
    if (!json_utils::ReadField(onvif, "firmware_version",
                               &config->onvif_firmware_version)) {
        return false;
    }
    return true;
}

}  // namespace

bool LoadRuntimeConfig(IConfig *config_store,
                       AppRuntimeConfig *runtime_config) {
    if (config_store == nullptr || runtime_config == nullptr) {
        return false;
    }
    ConfigJson root = ConfigJson::object();
    root["video"] = config_store->GetValue("video");
    root["network"] = config_store->GetValue("network");
    root["http"] = config_store->GetValue("http");
    root["rtsp"] = config_store->GetValue("rtsp");
    root["snapshot"] = config_store->GetValue("snapshot");
    root["webrtc"] = config_store->GetValue("webrtc");
    root["onvif"] = config_store->GetValue("onvif");
    return LoadRuntimeConfigFromRoot(root, runtime_config);
}

bool LoadRuntimeConfigFromRoot(const ConfigJson &root,
                               AppRuntimeConfig *runtime_config) {
    if (runtime_config == nullptr || !root.is_object()) {
        return false;
    }
    AppRuntimeConfig runtime;
    if (!root.contains("video") || !root.contains("network") ||
        !root.contains("http") || !root.contains("rtsp") ||
        !root.contains("snapshot") || !root.contains("webrtc") ||
        !root.contains("onvif")) {
        return false;
    }
    if (!ApplyRtspVideoCodecConfig(root.at("video"), &runtime)) {
        return false;
    }
    if (!ApplyNetworkConfig(root.at("network"), &runtime)) {
        return false;
    }
    if (!ApplyHttpConfig(root.at("http"), &runtime)) {
        return false;
    }
    if (!ApplyRtspConfig(root.at("rtsp"), &runtime)) {
        return false;
    }
    if (!ApplySnapshotConfig(root.at("snapshot"), &runtime)) {
        return false;
    }
    if (!ApplyWebrtcConfig(root.at("webrtc"), &runtime)) {
        return false;
    }
    if (!ApplyOnvifConfig(root.at("onvif"), &runtime)) {
        return false;
    }
    *runtime_config = runtime;
    return true;
}

}  // namespace live_stream
