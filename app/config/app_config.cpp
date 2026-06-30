#include "config/app_config.h"

#include "json_reader.h"

namespace live_stream {
namespace {

bool ParseRtspCodec(const std::string &value, Codec &codec) {
    if (value == "h265") {
        codec = Codec::kH265;
        return true;
    }
    if (value == "h264") {
        codec = Codec::kH264;
        return true;
    }
    return false;
}

bool ApplyRtspCodecConfig(const Json &video,
                          AppConfig &config) {
    if (!video.is_object()) {
        return false;
    }
    if (!video.contains("streams") || !video.at("streams").is_object()) {
        return false;
    }
    const Json &streams = video.at("streams");
    if (!streams.contains("main") || !streams.at("main").is_object() ||
        !streams.contains("sub") || !streams.at("sub").is_object()) {
        return false;
    }
    const Json &main = streams.at("main");
    const Json &sub = streams.at("sub");
    std::string codec;
    if (!json_reader::ReadField(main, "codec", &codec)) {
        return false;
    }
    if (!ParseRtspCodec(codec, config.rtsp_main_codec)) {
        return false;
    }
    if (!json_reader::ReadField(sub, "codec", &codec)) {
        return false;
    }
    if (!ParseRtspCodec(codec, config.rtsp_sub_codec)) {
        return false;
    }
    return true;
}

bool ApplyNetworkConfig(const Json &network, AppConfig &config) {
    if (!network.is_object()) {
        return false;
    }
    if (!json_reader::ReadField(network, "advertise_ip",
                               &config.advertise_host)) {
        return false;
    }
    if (config.advertise_host.empty()) {
        return false;
    }
    // default_ifname is optional; falls back to the struct default ("eth0").
    std::string ifname;
    if (json_reader::ReadField(network, "default_ifname", &ifname)) {
        if (!ifname.empty()) {
            config.network_ifname = ifname;
        }
    }
    return true;
}

bool ApplyHttpConfig(const Json &http, AppConfig &config) {
    if (!http.is_object()) {
        return false;
    }
    if (!json_reader::ReadField(http, "port", &config.http_port, 1,
                               65535)) {
        return false;
    }
    if (!json_reader::ReadField(http, "static_root", &config.static_root)) {
        return false;
    }
    return !config.static_root.empty();
}

bool ApplyRtspConfig(const Json &rtsp, AppConfig &config) {
    if (!rtsp.is_object()) {
        return false;
    }
    if (!json_reader::ReadField(rtsp, "port", &config.rtsp_port, 1,
                               65535)) {
        return false;
    }
    if (!json_reader::ReadField(rtsp, "auth_required",
                               &config.rtsp_auth_required)) {
        return false;
    }
    if (!json_reader::ReadField(rtsp, "max_sessions",
                               &config.rtsp_max_sessions, 1, 0xffffffffU)) {
        return false;
    }
    return true;
}

bool ApplySnapshotConfig(const Json &snapshot, AppConfig &config) {
    if (!snapshot.is_object()) {
        return false;
    }
    bool enabled = true;
    uint32_t jpeg_quality = 0;
    uint32_t timeout_ms = 0;
    if (!json_reader::ReadField(snapshot, "enabled", &enabled)) {
        return false;
    }
    if (!json_reader::ReadField(snapshot, "jpeg_quality", &jpeg_quality, 1,
                               100)) {
        return false;
    }
    if (!json_reader::ReadField(snapshot, "timeout_ms", &timeout_ms, 1,
                               0xffffffffU)) {
        return false;
    }
    return true;
}

bool ApplyWebrtcConfig(const Json &webrtc, AppConfig &config) {
    if (!webrtc.is_object()) {
        return false;
    }
    std::string public_ip = config.webrtc_public_ip;
    if (!json_reader::ReadField(webrtc, "enabled",
                               &config.webrtc_enabled)) {
        return false;
    }
    if (!json_reader::ReadField(webrtc, "prefer_tcp",
                               &config.webrtc_prefer_tcp)) {
        return false;
    }
    if (!json_reader::ReadField(webrtc, "local_port_base",
                               &config.webrtc_local_port_base, 1, 65535)) {
        return false;
    }
    if (!json_reader::ReadField(webrtc, "max_peers",
                               &config.webrtc_max_peers, 1, 0xffffffffU)) {
        return false;
    }
    if (webrtc.contains("public_ip") &&
        !json_reader::ReadField(webrtc, "public_ip", &public_ip)) {
        return false;
    }
    config.webrtc_public_ip = public_ip;
    if (!webrtc.contains("ice_servers") ||
        !webrtc.at("ice_servers").is_array()) {
        return false;
    }
    const Json &ice_servers = webrtc.at("ice_servers");
    config.webrtc_ice_servers.clear();
    for (const Json &item : ice_servers) {
        if (!item.is_object()) {
            return false;
        }
        WebrtcIceServer server;
        if (!json_reader::ReadField(item, "url", &server.url)) {
            return false;
        }
        if (!json_reader::ReadField(item, "username", &server.username)) {
            return false;
        }
        if (!json_reader::ReadField(item, "credential", &server.credential)) {
            return false;
        }
        config.webrtc_ice_servers.push_back(server);
    }
    return true;
}

bool ApplyOnvifConfig(const Json &onvif, AppConfig &config) {
    if (!onvif.is_object()) {
        return false;
    }
    std::string advertise_ip;
    if (!json_reader::ReadField(onvif, "device_service_port",
                               &config.onvif_device_port, 1, 65535)) {
        return false;
    }
    if (!json_reader::ReadField(onvif, "discovery_port",
                               &config.onvif_discovery_port, 1, 65535)) {
        return false;
    }
    if (!json_reader::ReadField(onvif, "discovery_enabled",
                               &config.onvif_discovery_enabled)) {
        return false;
    }
    if (!json_reader::ReadField(onvif, "auth_required",
                               &config.onvif_auth_required)) {
        return false;
    }
    if (!json_reader::ReadField(onvif, "advertise_ip", &advertise_ip) ||
        advertise_ip.empty()) {
        return false;
    }
    if (!json_reader::ReadField(onvif, "manufacturer",
                               &config.onvif_manufacturer)) {
        return false;
    }
    if (!json_reader::ReadField(onvif, "model", &config.onvif_model)) {
        return false;
    }
    return true;
}

}  // namespace

bool LoadAppConfig(IConfig *config_source,
                   AppConfig *app_config) {
    if (config_source == nullptr || app_config == nullptr) {
        return false;
    }
    Json root = Json::object();
    root["video"] = config_source->Get("video");
    root["network"] = config_source->Get("network");
    root["http"] = config_source->Get("http");
    root["rtsp"] = config_source->Get("rtsp");
    root["snapshot"] = config_source->Get("snapshot");
    root["webrtc"] = config_source->Get("webrtc");
    root["onvif"] = config_source->Get("onvif");
    return LoadAppConfigFromRoot(root, app_config);
}

bool LoadAppConfigFromRoot(const Json &root,
                           AppConfig *app_config) {
    if (app_config == nullptr || !root.is_object()) {
        return false;
    }
    AppConfig app_config_value;
    if (!root.contains("video") || !root.contains("network") ||
        !root.contains("http") || !root.contains("rtsp") ||
        !root.contains("snapshot") || !root.contains("webrtc") ||
        !root.contains("onvif")) {
        return false;
    }
    if (!ApplyRtspCodecConfig(root.at("video"), app_config_value)) {
        return false;
    }
    if (!ApplyNetworkConfig(root.at("network"), app_config_value)) {
        return false;
    }
    if (!ApplyHttpConfig(root.at("http"), app_config_value)) {
        return false;
    }
    if (!ApplyRtspConfig(root.at("rtsp"), app_config_value)) {
        return false;
    }
    if (!ApplySnapshotConfig(root.at("snapshot"), app_config_value)) {
        return false;
    }
    if (!ApplyWebrtcConfig(root.at("webrtc"), app_config_value)) {
        return false;
    }
    if (!ApplyOnvifConfig(root.at("onvif"), app_config_value)) {
        return false;
    }
    *app_config = app_config_value;
    return true;
}

}  // namespace live_stream
