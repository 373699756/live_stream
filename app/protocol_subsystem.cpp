#include "protocol_subsystem.h"

#include <cctype>
#include <string>

#include "core_subsystem.h"
#include "device_subsystem.h"
#include "http_console.h"
#include "infra/log.h"
#include "media_subsystem.h"

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

struct ProtocolRuntimeRefs {
    CoreSubsystem *core = nullptr;
    DeviceRefs device;
    MediaRefs media;
    NetEngine *net_engine = nullptr;
    IRtsp *rtsp = nullptr;
    OnvifServer *onvif = nullptr;
    IWebrtc *webrtc = nullptr;
    IMediaPipeline *media_pipeline = nullptr;
};

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

OnvifServerOptions BuildOnvifOptions(const AppRuntimeConfig &runtime_config) {
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
    options.snapshot_main_path = runtime_config.snapshot_main_path;
    options.snapshot_sub_path = runtime_config.snapshot_sub_path;
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

}  // namespace

ProtocolSubsystem &ProtocolSubsystem::Get() {
    static ProtocolSubsystem subsystem;
    return subsystem;
}

bool ProtocolSubsystem::Start(const AppRuntimeConfig &runtime_config,
                              CoreSubsystem &core_subsystem,
                              const DeviceRefs &device_refs,
                              const MediaRefs &media_refs) {
    if (started_) {
        return true;
    }

    ProtocolRuntimeRefs refs;
    refs.core = &core_subsystem;
    refs.device = device_refs;
    refs.media = media_refs;

    if (!net_callback_executor_) {
        net_callback_executor_.reset(new infra::Executor());
    }
    const infra::ExecutorOptions callback_executor_options =
        BuildNetCallbackExecutorOptions();
    if (!net_callback_executor_->Start(callback_executor_options)) {
        Error("app", "Start net callback executor failed");
        Stop();
        return false;
    }

    const NetEngineOptions net_options =
        BuildNetEngineOptions(net_callback_executor_.get());
    net_engine_ = CreateNetEngine(net_options);
    if (!net_engine_ || !net_engine_->Start()) {
        Error("app", "Start net engine failed");
        Stop();
        return false;
    }
    refs.net_engine = net_engine_.get();

    const MediaPipelineOptions media_pipeline_options =
        BuildMediaPipelineOptions();
    const MediaPipelineDependencies media_pipeline_dependencies =
        BuildMediaPipelineDependencies(refs);
    media_pipeline_ =
        CreateMediaPipeline(media_pipeline_options,
                                 media_pipeline_dependencies);
    if (!media_pipeline_ || !media_pipeline_->Start()) {
        Error("app", "Start media_pipeline failed");
        Stop();
        return false;
    }
    refs.media_pipeline = media_pipeline_.get();

    const RtspOptions rtsp_options = BuildRtspOptions(runtime_config);
    const RtspDependencies rtsp_dependencies = BuildRtspDependencies(refs);
    rtsp_ = CreateRtsp(rtsp_options, rtsp_dependencies);
    if (!rtsp_ || !rtsp_->Start()) {
        Error("app",
                        "Start rtsp failed, continue without RTSP: "
                        "listen=%s:%u",
                        runtime_config.listen_ip.c_str(),
                        static_cast<unsigned>(runtime_config.rtsp_port));
        if (rtsp_) {
            rtsp_->Stop();
        }
        rtsp_.reset();
        refs.rtsp = nullptr;
    } else {
        refs.rtsp = rtsp_.get();
        const RtspListenAddress rtsp_address = rtsp_->LocalAddress();
        Info("app", "RTSP listening %s:%u",
                       rtsp_address.ip.c_str(),
                       static_cast<unsigned>(rtsp_address.port));
    }

    const WebrtcOptions webrtc_options =
        BuildWebrtcOptions(runtime_config, refs);
    const WebrtcDependencies webrtc_dependencies =
        BuildWebrtcDependencies(refs);
    webrtc_ = CreateWebrtc(webrtc_options, webrtc_dependencies);
    if (!webrtc_ || !webrtc_->Start()) {
        Error(
            "app",
            "Start webrtc failed, continue without WebRTC: "
            "enabled=%d base=%u",
            runtime_config.webrtc_enabled ? 1 : 0,
            static_cast<unsigned>(runtime_config.webrtc_local_port_base));
        if (webrtc_) {
            webrtc_->Stop();
        }
        webrtc_.reset();
        refs.webrtc = nullptr;
    } else {
        refs.webrtc = webrtc_.get();
    }

    const OnvifServerOptions onvif_options = BuildOnvifOptions(runtime_config);
    const OnvifServerDependencies onvif_dependencies =
        BuildOnvifDependencies(refs);
    onvif_ = CreateOnvifServer(onvif_options, onvif_dependencies);
    if (!onvif_ || !onvif_->Start()) {
        Error("app",
                        "Start onvif failed, continue without ONVIF: "
                        "device_port=%u",
                        static_cast<unsigned>(runtime_config.onvif_device_port));
        if (onvif_) {
            onvif_->Stop();
        }
        onvif_.reset();
        refs.onvif = nullptr;
    } else {
        refs.onvif = onvif_.get();
        Info("app", "ONVIF started listen=%s:%u discovery=%u",
                       runtime_config.listen_ip.c_str(),
                       static_cast<unsigned>(runtime_config.onvif_device_port),
                       static_cast<unsigned>(runtime_config.onvif_discovery_port));
    }

    const HttpOptions http_options = BuildHttpOptions(runtime_config);
    http_ = CreateHttpConsole(
        http_options, refs.net_engine,
        refs.core != nullptr ? refs.core->auth() : nullptr,
        refs.core != nullptr ? refs.core->logger() : nullptr,
        refs.core != nullptr ? refs.core->config() : nullptr,
        refs.device.network, refs.device.time, refs.device.alarm,
        refs.device.upgrade, refs.device.system, refs.rtsp,
        refs.onvif, refs.media.ai, refs.media.device_media,
        refs.media.snapshot, refs.webrtc, refs.media_pipeline,
        refs.media_pipeline, refs.media_pipeline);
    if (!http_ || !http_->Start()) {
        Error("app", "Start http failed: listen=%s:%u root=%s",
                        runtime_config.listen_ip.c_str(),
                        static_cast<unsigned>(runtime_config.http_port),
                        runtime_config.static_root.c_str());
        Stop();
        return false;
    }
    const HttpListenAddress http_address = http_->LocalAddress();
    Info("app", "HTTP listening %s:%u root=%s",
                   http_address.ip.c_str(),
                   static_cast<unsigned>(http_address.port),
                   runtime_config.static_root.c_str());

    const NetAdaptiveOptions net_adaptive_options =
        BuildNetAdaptiveOptions();
    const NetAdaptiveDependencies net_adaptive_dependencies =
        BuildNetAdaptiveDependencies(refs);
    net_adaptive_ =
        CreateNetAdaptive(net_adaptive_options, net_adaptive_dependencies);
    if (!net_adaptive_ || !net_adaptive_->Start()) {
        Error("app", "Start net_adaptive failed");
        Stop();
        return false;
    }

    started_ = true;
    return true;
}

void ProtocolSubsystem::Stop() {
    if (net_adaptive_) {
        Info("app", "Stop net_adaptive begin");
        net_adaptive_->Stop();
        Info("app", "Stop net_adaptive done");
    }
    if (http_) {
        Info("app", "Stop http begin");
        http_->Stop();
        Info("app", "Stop http done");
    }
    if (onvif_) {
        Info("app", "Stop onvif begin");
        onvif_->Stop();
        Info("app", "Stop onvif done");
    }
    if (webrtc_) {
        Info("app", "Stop webrtc begin");
        webrtc_->Stop();
        Info("app", "Stop webrtc done");
    }
    if (rtsp_) {
        Info("app", "Stop rtsp begin");
        rtsp_->Stop();
        Info("app", "Stop rtsp done");
    }
    if (media_pipeline_) {
        Info("app", "Stop media_pipeline begin");
        media_pipeline_->Stop();
        Info("app", "Stop media_pipeline done");
    }
    if (net_engine_) {
        Info("app", "Stop net engine begin");
        net_engine_->Stop();
        net_engine_.reset();
        Info("app", "Stop net engine done");
    }
    if (net_callback_executor_) {
        Info("app", "Stop net callback executor begin");
        net_callback_executor_->Stop(infra::StopMode::kDiscard);
        Info("app", "Stop net callback executor done");
    }
    http_.reset();
    net_adaptive_.reset();
    onvif_.reset();
    media_pipeline_.reset();
    webrtc_.reset();
    rtsp_.reset();
    started_ = false;
}

ProtocolRefs ProtocolSubsystem::refs() const {
    ProtocolRefs refs;
    refs.rtsp = rtsp_.get();
    refs.webrtc = webrtc_.get();
    refs.onvif = onvif_.get();
    refs.http = http_.get();
    return refs;
}

}  // namespace live_stream
