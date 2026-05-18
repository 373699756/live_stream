#ifndef LIVE_STREAM_APP_RUNTIME_CONFIG_H_
#define LIVE_STREAM_APP_RUNTIME_CONFIG_H_

#include <cstdint>
#include <string>
#include <vector>

#include "config_service.h"
#include "media/stream_types.h"
#include "webrtc_service.h"

namespace live_stream {

struct AppRuntimeConfig {
    std::string listen_ip = "0.0.0.0";
    std::string advertise_host = "127.0.0.1";
    std::string static_root = "web";
    std::string snapshot_main_path = "/api/snapshot/main.jpg";
    std::string snapshot_sub_path = "/api/snapshot/sub.jpg";
    std::string onvif_manufacturer = "CBinary";
    std::string onvif_model = "live_stream_ipc";
    std::string onvif_firmware_version = "0.1.0";
    // Primary network interface used by NetworkService and platform adapter.
    // Defaults to "eth0"; read from network.default_ifname in the config.
    std::string network_ifname = "eth0";
    uint16_t http_port = 80;
    uint16_t rtsp_port = 554;
    uint16_t onvif_device_port = 8000;
    uint16_t onvif_discovery_port = 3702;
    uint16_t webrtc_local_port_base = 16000;
    uint32_t rtsp_max_sessions = 16;
    uint32_t webrtc_max_peers = 4;
    VideoCodec rtsp_main_codec = VideoCodec::kH264;
    VideoCodec rtsp_sub_codec = VideoCodec::kH264;
    bool rtsp_auth_required = true;
    bool onvif_auth_required = false;
    bool onvif_discovery_enabled = true;
    bool webrtc_enabled = true;
    bool webrtc_prefer_tcp = false;
    std::vector<WebrtcIceServer> webrtc_ice_servers;
};

bool LoadRuntimeConfig(IConfigService *config_service,
                       AppRuntimeConfig *config);

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_RUNTIME_CONFIG_H_
