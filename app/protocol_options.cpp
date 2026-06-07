#include "protocol_options.h"

#include "core_subsystem.h"
#include "infra/log.h"

#include <cctype>
#include <string>

namespace live_stream {
namespace {

constexpr uint32_t kNetIoThreadCount = 1;
constexpr uint32_t kNetCallbackWorkerCount = 1;
constexpr uint32_t kNetCallbackQueueCapacity = 4096;
constexpr uint32_t kHttpStreamExecutorWorkerCount = 4;
constexpr uint32_t kHttpStreamExecutorQueueCapacity = 256;
constexpr uint32_t kHttpControlExecutorWorkerCount = 1;
constexpr uint32_t kHttpControlExecutorQueueCapacity = 16;
constexpr uint32_t kHttpMaxRequestsPerConnection = 32;
constexpr uint32_t kHttpMaxRequestBodyBytes = 32U * 1024U * 1024U;
constexpr uint32_t kHttpRequestTimeoutMs = 60000;
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

bool ParseIpv4Octets(const std::string &ip, int octets[4]) {
    if (octets == nullptr || ip.empty()) {
        return false;
    }
    int parsed_count = 0;
    std::size_t start = 0;
    while (start < ip.size()) {
        if (parsed_count >= 4) {
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
        octets[parsed_count] = octet;
        ++parsed_count;
        start = end + 1;
    }
    return parsed_count == 4 && ip[ip.size() - 1] != '.';
}

bool IsUsableWebrtcPublicIp(const std::string &public_ip) {
    int octets[4] = {};
    if (!ParseIpv4Octets(public_ip, octets)) {
        return false;
    }
    return octets[0] != 0 && octets[0] != 127;
}

std::string ResolveWebrtcPublicIp(
    const AppRuntimeConfig &runtime_config,
    const ProtocolRuntimeRefs &refs) {
    if (!runtime_config.webrtc_enabled) {
        return runtime_config.webrtc_public_ip;
    }

    if (!IsAutoWebrtcPublicIp(runtime_config.webrtc_public_ip)) {
        if (!IsUsableWebrtcPublicIp(runtime_config.webrtc_public_ip)) {
            Error("app", "WebRTC public_ip invalid ip=%s",
                  runtime_config.webrtc_public_ip.c_str());
            return std::string();
        }
        return runtime_config.webrtc_public_ip;
    }

    if (refs.device.network != nullptr) {
        const NetworkInterfaceStatus status =
            refs.device.network->GetInterfaceStatus(
                runtime_config.network_ifname);
        if (IsUsableWebrtcPublicIp(status.static_ipv4.address)) {
            Info("app", "WebRTC public_ip auto resolved ifname=%s ip=%s",
                 runtime_config.network_ifname.c_str(),
                 status.static_ipv4.address.c_str());
            return status.static_ipv4.address;
        }
        Warn("app", "WebRTC public_ip auto unavailable ifname=%s ip=%s",
             runtime_config.network_ifname.c_str(),
             status.static_ipv4.address.c_str());
    }

    if (IsUsableWebrtcPublicIp(runtime_config.advertise_host)) {
        Info("app", "WebRTC public_ip fallback advertise_ip=%s",
             runtime_config.advertise_host.c_str());
        return runtime_config.advertise_host;
    }
    return std::string();
}

}  // namespace

infra::ExecutorOptions BuildNetCallbackExecutorOptions() {
    infra::ExecutorOptions options;
    options.worker_count = kNetCallbackWorkerCount;
    options.queue_capacity = kNetCallbackQueueCapacity;
    return options;
}

NetEngineOptions BuildNetEngineOptions(infra::Executor *callback_executor) {
    NetEngineOptions options;
    options.io_threads = kNetIoThreadCount;
    options.callback_mode = CallbackMode::kPostToExecutor;
    options.callback_executor = callback_executor;
    return options;
}

RtspOptions BuildRtspOptions(const AppRuntimeConfig &runtime_config) {
    RtspOptions options;
    options.listen_ip = runtime_config.listen_ip;
    options.listen_port = runtime_config.rtsp_port;
    options.max_sessions = runtime_config.rtsp_max_sessions;
    options.enable_auth = runtime_config.rtsp_auth_required;
    options.main_video_codec = runtime_config.rtsp_main_codec;
    options.sub_video_codec = runtime_config.rtsp_sub_codec;
    return options;
}

RtspDependencies BuildRtspDependencies(const ProtocolRuntimeRefs &refs) {
    RtspDependencies dependencies;
    dependencies.net_engine = refs.net_engine;
    dependencies.auth = refs.core != nullptr ? refs.core->auth() : nullptr;
    dependencies.event = refs.core != nullptr ? refs.core->event() : nullptr;
    dependencies.media_source = refs.media_pipeline;
    return dependencies;
}

WebrtcOptions BuildWebrtcOptions(const AppRuntimeConfig &runtime_config,
                                 const ProtocolRuntimeRefs &refs) {
    WebrtcOptions options;
    options.enabled = runtime_config.webrtc_enabled;
    options.local_port_base = runtime_config.webrtc_local_port_base;
    options.max_peers = runtime_config.webrtc_max_peers;
    options.prefer_tcp = runtime_config.webrtc_prefer_tcp;
    options.public_ip = ResolveWebrtcPublicIp(runtime_config, refs);
    if (options.enabled && options.public_ip.empty()) {
        Error("app",
              "WebRTC disabled: public_ip is not resolvable ifname=%s",
              runtime_config.network_ifname.c_str());
        options.enabled = false;
    }
    options.ice_servers = runtime_config.webrtc_ice_servers;
    return options;
}

WebrtcDependencies BuildWebrtcDependencies(
    const ProtocolRuntimeRefs &refs) {
    WebrtcDependencies dependencies;
    dependencies.net_engine = refs.net_engine;
    dependencies.media_source = refs.media_pipeline;
    return dependencies;
}

MediaPipelineOptions BuildMediaPipelineOptions() {
    MediaPipelineOptions options;
    return options;
}

MediaPipelineDependencies BuildMediaPipelineDependencies(
    const ProtocolRuntimeRefs &refs) {
    MediaPipelineDependencies dependencies;
    dependencies.device_media = refs.media.device_media;
    return dependencies;
}

OnvifServerOptions BuildOnvifOptions(
    const AppRuntimeConfig &runtime_config) {
    OnvifServerOptions options;
    options.listen_ip = runtime_config.listen_ip;
    options.advertise_ip = runtime_config.advertise_host;
    options.device_service_port = runtime_config.onvif_device_port;
    options.discovery_port = runtime_config.onvif_discovery_port;
    options.discovery_enabled = runtime_config.onvif_discovery_enabled;
    options.enable_auth = runtime_config.onvif_auth_required;
    options.manufacturer = runtime_config.onvif_manufacturer;
    options.model = runtime_config.onvif_model;
    options.firmware_version = runtime_config.onvif_firmware_version;
    options.http_port = runtime_config.http_port;
    return options;
}

OnvifServerDependencies BuildOnvifDependencies(
    const ProtocolRuntimeRefs &refs) {
    OnvifServerDependencies dependencies;
    dependencies.net_engine = refs.net_engine;
    dependencies.auth = refs.core != nullptr ? refs.core->auth() : nullptr;
    dependencies.event = refs.core != nullptr ? refs.core->event() : nullptr;
    dependencies.system = refs.device.system;
    dependencies.time = refs.device.time;
    dependencies.device_media = refs.media.device_media;
    dependencies.rtsp = refs.rtsp;
    return dependencies;
}

HttpOptions BuildHttpOptions(const AppRuntimeConfig &runtime_config) {
    HttpOptions options;
    options.listen_ip = runtime_config.listen_ip;
    options.listen_port = runtime_config.http_port;
    options.static_root = runtime_config.static_root;
    options.enable_static_files = true;
    options.enable_keep_alive = true;
    options.stream_executor_worker_count = kHttpStreamExecutorWorkerCount;
    options.stream_executor_queue_capacity = kHttpStreamExecutorQueueCapacity;
    options.control_executor_worker_count = kHttpControlExecutorWorkerCount;
    options.control_executor_queue_capacity = kHttpControlExecutorQueueCapacity;
    options.max_requests_per_connection = kHttpMaxRequestsPerConnection;
    options.max_request_body_bytes = kHttpMaxRequestBodyBytes;
    options.request_timeout_ms = kHttpRequestTimeoutMs;
    options.connection_idle_timeout_ms = kHttpConnectionIdleTimeoutMs;
    return options;
}

HttpConsoleDependencies BuildHttpConsoleDependencies(
    const ProtocolRuntimeRefs &refs) {
    HttpConsoleDependencies dependencies;
    dependencies.net_engine = refs.net_engine;
    dependencies.auth = refs.core != nullptr ? refs.core->auth() : nullptr;
    dependencies.logger = refs.core != nullptr ? refs.core->logger() : nullptr;
    dependencies.config = refs.core != nullptr ? refs.core->config() : nullptr;
    dependencies.network_config = refs.device.network;
    dependencies.time = refs.device.time;
    dependencies.alarm = refs.device.alarm;
    dependencies.upgrade = refs.device.upgrade;
    dependencies.system = refs.device.system;
    dependencies.rtsp = refs.rtsp;
    dependencies.onvif = refs.onvif;
    dependencies.ai = refs.media.ai;
    dependencies.device_media = refs.media.device_media;
    dependencies.snapshot = refs.media.snapshot;
    dependencies.webrtc = refs.webrtc;
    dependencies.media_source = refs.media_pipeline;
    dependencies.media_flv_source = refs.media_pipeline;
    dependencies.media_mjpeg_source = refs.media_pipeline;
    return dependencies;
}

NetAdaptiveOptions BuildNetAdaptiveOptions() {
    NetAdaptiveOptions options;
    return options;
}

NetAdaptiveDependencies BuildNetAdaptiveDependencies(
    const ProtocolRuntimeRefs &refs) {
    NetAdaptiveDependencies dependencies;
    dependencies.net_engine = refs.net_engine;
    dependencies.rtsp = refs.rtsp;
    dependencies.webrtc = refs.webrtc;
    dependencies.media_source = refs.media_pipeline;
    return dependencies;
}

}  // namespace live_stream
