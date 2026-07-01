#include "subsystems/protocol_options.h"

#include "http_dependencies.h"
#include "infra/log.h"
#include "subsystems/foundation_subsystem.h"

#include <cctype>
#include <string>

namespace live_stream {
namespace {

constexpr uint32_t kNetIoThreads = 2;
constexpr uint32_t kNetCallbackQueueCapacity = 4096;
constexpr uint32_t kHttpStreamWorkers = 4;
constexpr uint32_t kHttpStreamQueueCapacity = 256;
constexpr uint32_t kHttpControlWorkers = 1;
constexpr uint32_t kHttpControlQueueCapacity = 16;
constexpr uint32_t kHttpMaxRequestsPerConnection = 32;
constexpr uint32_t kHttpMaxRequestBodyBytes = 16U * 1024U * 1024U;
constexpr uint32_t kHttpRequestTimeoutMs = 10U * 60U * 1000U;
constexpr uint32_t kHttpConnectionIdleTimeoutMs = 60000;
constexpr const char *kWebrtcPublicIpAuto = "auto";

std::string ToLowerAscii(std::string text) {
    for (char &ch : text) {
        ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool IsAutoWebrtcPublicIp(const std::string &public_ip) {
    return public_ip.empty() || public_ip == "0.0.0.0" ||
           ToLowerAscii(public_ip) == kWebrtcPublicIpAuto;
}

bool ParseIpv4Octets(const std::string &ip, int (&octets)[4]) {
    if (ip.empty()) {
        return false;
    }
    int parsed_octets = 0;
    std::size_t start = 0;
    while (start < ip.size()) {
        if (parsed_octets >= 4) {
            return false;
        }
        std::size_t end = ip.find('.', start);
        if (end == std::string::npos) {
            end = ip.size();
        }
        if (end == start || end - start > 3) {
            return false;
        }
        int octet = 0;
        for (std::size_t i = start; i < end; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(ip[i]))) {
                return false;
            }
            octet = octet * 10 + ip[i] - '0';
        }
        if (octet > 255) {
            return false;
        }
        octets[parsed_octets] = octet;
        ++parsed_octets;
        start = end + 1;
    }
    return parsed_octets == 4 && ip[ip.size() - 1] != '.';
}

bool IsUsableWebrtcPublicIp(const std::string &public_ip) {
    int octets[4] = {};
    if (!ParseIpv4Octets(public_ip, octets)) {
        return false;
    }
    return octets[0] != 0 && octets[0] != 127;
}

std::string ResolveWebrtcPublicIp(
    const AppConfig &app_config,
    const ProtocolStartupRefs &refs) {
    if (!app_config.webrtc_enabled) {
        return app_config.webrtc_public_ip;
    }

    if (!IsAutoWebrtcPublicIp(app_config.webrtc_public_ip)) {
        if (!IsUsableWebrtcPublicIp(app_config.webrtc_public_ip)) {
            Error("app", "WebRTC public_ip invalid ip=%s",
                  app_config.webrtc_public_ip.c_str());
            return std::string();
        }
        return app_config.webrtc_public_ip;
    }

    if (refs.device.network != nullptr) {
        const NetInterfaceInfo interface_info =
            refs.device.network->GetInterfaceInfo(
                app_config.network_ifname);
        if (IsUsableWebrtcPublicIp(interface_info.static_ipv4.address)) {
            Info("app", "WebRTC public_ip auto resolved ifname=%s ip=%s",
                 app_config.network_ifname.c_str(),
                 interface_info.static_ipv4.address.c_str());
            return interface_info.static_ipv4.address;
        }
        Warn("app", "WebRTC public_ip auto unavailable ifname=%s ip=%s",
             app_config.network_ifname.c_str(),
             interface_info.static_ipv4.address.c_str());
    }

    if (IsUsableWebrtcPublicIp(app_config.advertise_host)) {
        Info("app", "WebRTC public_ip fallback advertise_ip=%s",
             app_config.advertise_host.c_str());
        return app_config.advertise_host;
    }
    return std::string();
}

}  // namespace

event::LoopOptions BuildNetCallbackOptions() {
    event::LoopOptions options;
    options.name = "net-callback";
    options.queue_capacity = kNetCallbackQueueCapacity;
    return options;
}

NetIoOptions BuildNetIoOptions(event::Loop *callback_loop) {
    NetIoOptions options;
    options.io_threads = kNetIoThreads;
    options.enable_thread_affinity = false;
    options.callback_mode = CallbackMode::kPostToLoop;
    options.callback_loop = callback_loop;
    return options;
}

RtspOptions BuildRtspOptions(const AppConfig &app_config) {
    RtspOptions options;
    options.listen_ip = app_config.listen_ip;
    options.listen_port = app_config.rtsp_port;
    options.max_sessions = app_config.rtsp_max_sessions;
    options.enable_auth = app_config.rtsp_auth_required;
    options.main_video_codec = app_config.rtsp_main_codec;
    options.sub_video_codec = app_config.rtsp_sub_codec;
    return options;
}

WebrtcOptions BuildWebrtcOptions(const AppConfig &app_config,
                                 const ProtocolStartupRefs &refs) {
    WebrtcOptions options;
    options.enabled = app_config.webrtc_enabled;
    options.local_port_base = app_config.webrtc_local_port_base;
    options.max_peers = app_config.webrtc_max_peers;
    options.prefer_tcp = app_config.webrtc_prefer_tcp;
    options.public_ip = ResolveWebrtcPublicIp(app_config, refs);
    if (options.enabled && options.public_ip.empty()) {
        Error("app",
              "WebRTC disabled: public_ip is not resolvable ifname=%s",
              app_config.network_ifname.c_str());
        options.enabled = false;
    }
    options.ice_servers = app_config.webrtc_ice_servers;
    return options;
}

OnvifServerOptions BuildOnvifOptions(
    const AppConfig &app_config) {
    OnvifServerOptions options;
    options.listen_ip = app_config.listen_ip;
    options.advertise_ip = app_config.advertise_host;
    options.device_service_port = app_config.onvif_device_port;
    options.discovery_port = app_config.onvif_discovery_port;
    options.discovery_enabled = app_config.onvif_discovery_enabled;
    options.enable_auth = app_config.onvif_auth_required;
    options.manufacturer = app_config.onvif_manufacturer;
    options.model = app_config.onvif_model;
    options.firmware_version = LIVE_STREAM_RELEASE_VERSION;
    options.http_port = app_config.http_port;
    return options;
}

OnvifServerDependencies BuildOnvifDependencies(
    const ProtocolStartupRefs &refs,
    FoundationSubsystem &foundation) {
    OnvifServerDependencies dependencies;
    dependencies.net_io = refs.net_io;
    dependencies.net_loop = refs.onvif_loop;
    dependencies.auth = foundation.auth();
    dependencies.event = foundation.event();
    dependencies.system = refs.device.system;
    dependencies.time = refs.device.time;
    dependencies.device = refs.media.device;
    dependencies.rtsp = refs.rtsp;
    return dependencies;
}

HttpOptions BuildHttpOptions(const AppConfig &app_config) {
    HttpOptions options;
    options.listen_ip = app_config.listen_ip;
    options.listen_port = app_config.http_port;
    options.static_root = app_config.static_root;
    options.enable_static_files = true;
    options.enable_keep_alive = true;
    options.stream_executor_workers = kHttpStreamWorkers;
    options.stream_executor_queue_capacity = kHttpStreamQueueCapacity;
    options.control_executor_workers = kHttpControlWorkers;
    options.control_executor_queue_capacity = kHttpControlQueueCapacity;
    options.max_requests_per_connection = kHttpMaxRequestsPerConnection;
    options.max_request_body_bytes = kHttpMaxRequestBodyBytes;
    options.request_timeout_ms = kHttpRequestTimeoutMs;
    options.connection_idle_timeout_ms = kHttpConnectionIdleTimeoutMs;
    return options;
}

HttpDependencies BuildHttpDependencies(
    const ProtocolStartupRefs &refs,
    FoundationSubsystem &foundation) {
    HttpDependencies dependencies;
    dependencies.net_io = refs.net_io;
    dependencies.net_loop = refs.http_loop;
    dependencies.auth = foundation.auth();
    dependencies.logger = foundation.logger();
    dependencies.config = foundation.config();
    dependencies.network = refs.device.network;
    dependencies.time = refs.device.time;
    dependencies.alarm = refs.device.alarm;
    dependencies.upgrade = refs.device.upgrade;
    dependencies.system = refs.device.system;
    dependencies.rtsp_session_reader = refs.rtsp;
    dependencies.onvif_reader = refs.onvif;
    dependencies.ai = refs.media.ai;
    dependencies.device = refs.media.device;
    dependencies.webrtc = refs.webrtc;
    dependencies.webrtc_reader = refs.webrtc;
    dependencies.event = foundation.event();
    return dependencies;
}

NetStatOptions BuildNetStatOptions() {
    NetStatOptions options;
    return options;
}

}  // namespace live_stream
