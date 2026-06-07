#include "protocol_subsystem.h"

#include <cctype>
#include <string>
#include <vector>

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

ConfigResult RejectRuntimeConfigChange(const char *field) {
    return ConfigResult::Failure(field == nullptr ? "" : field,
                                 "restart required");
}

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
           current_config.snapshot_main_path !=
               next_config.snapshot_main_path ||
           current_config.snapshot_sub_path != next_config.snapshot_sub_path ||
           current_config.http_port != next_config.http_port;
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

    config_ = core_subsystem.config();
    network_config_ = device_refs.network;
    runtime_config_ = runtime_config;
    if (!InstallRuntimeConfigAttachments()) {
        Error("app", "Install protocol runtime config attachments failed");
        Stop();
        return false;
    }

    started_ = true;
    return true;
}

bool ProtocolSubsystem::InstallRuntimeConfigAttachments() {
    if (config_ == nullptr) {
        return false;
    }
    const char *scopes[] = {"http", "rtsp", "webrtc", "onvif"};
    for (const char *scope : scopes) {
        ConfigAttachment attachment;
        attachment.validate = [this, scope](const ConfigJson &value) {
            return ValidateRuntimeConfigUpdate(scope, value);
        };
        attachment.apply = [this, scope](const ConfigJson &value) {
            return ApplyRuntimeConfigUpdate(scope, value);
        };
        if (!config_->AttachConfig(scope, attachment)) {
            for (const char *attached_scope : scopes) {
                if (std::string(attached_scope) == scope) {
                    break;
                }
                static_cast<void>(config_->DetachConfig(attached_scope));
            }
            return false;
        }
    }
    return true;
}

void ProtocolSubsystem::DetachRuntimeConfigAttachments() {
    if (config_ == nullptr) {
        return;
    }
    static_cast<void>(config_->DetachConfig("http"));
    static_cast<void>(config_->DetachConfig("rtsp"));
    static_cast<void>(config_->DetachConfig("webrtc"));
    static_cast<void>(config_->DetachConfig("onvif"));
    config_ = nullptr;
    network_config_ = nullptr;
}

bool ProtocolSubsystem::BuildNextRuntimeConfig(
    const std::string &scope,
    const ConfigJson &value,
    AppRuntimeConfig *next_config) const {
    if (config_ == nullptr || next_config == nullptr) {
        return false;
    }
    ConfigJson root = ConfigJson::object();
    const char *scopes[] = {"video", "network", "http", "rtsp",
                            "snapshot", "webrtc", "onvif"};
    for (const char *item : scopes) {
        if (scope == item) {
            root[item] = value;
        } else {
            root[item] = config_->GetValue(item);
        }
    }
    return LoadRuntimeConfigFromRoot(root, next_config);
}

ConfigResult ProtocolSubsystem::ValidateRuntimeConfigUpdate(
    const std::string &scope,
    const ConfigJson &value) {
    AppRuntimeConfig next_config;
    if (!BuildNextRuntimeConfig(scope, value, &next_config)) {
        return ConfigResult::Failure("", "invalid runtime config");
    }
    return ValidateRuntimeConfigScope(runtime_config_, next_config, scope);
}

ConfigResult ProtocolSubsystem::ApplyRuntimeConfigUpdate(
    const std::string &scope,
    const ConfigJson &value) {
    AppRuntimeConfig next_config;
    if (!BuildNextRuntimeConfig(scope, value, &next_config)) {
        return ConfigResult::Failure("", "invalid runtime config");
    }
    const ConfigResult validate_result =
        ValidateRuntimeConfigScope(runtime_config_, next_config, scope);
    if (!validate_result.ok) {
        return validate_result;
    }

    if (scope == "rtsp" &&
        IsRtspRuntimeChanged(runtime_config_, next_config)) {
        if (rtsp_ == nullptr) {
            return ConfigResult::Failure("", "rtsp unavailable");
        }
        if (!rtsp_->ApplyOptions(BuildRtspOptions(next_config))) {
            return ConfigResult::Failure("", "apply rtsp config failed");
        }
    }
    if (scope == "webrtc" &&
        IsWebrtcRuntimeChanged(runtime_config_, next_config)) {
        if (webrtc_ == nullptr) {
            return ConfigResult::Failure("", "webrtc unavailable");
        }
        ProtocolRuntimeRefs refs;
        refs.device.network = network_config_;
        refs.net_engine = net_engine_.get();
        refs.rtsp = rtsp_.get();
        refs.onvif = onvif_.get();
        refs.webrtc = webrtc_.get();
        refs.media_pipeline = media_pipeline_.get();
        const WebrtcOptions options = BuildWebrtcOptions(next_config, refs);
        if (!webrtc_->ApplyOptions(options)) {
            return ConfigResult::Failure("", "apply webrtc config failed");
        }
    }
    if (scope == "onvif" &&
        IsOnvifRuntimeChanged(runtime_config_, next_config)) {
        if (onvif_ == nullptr) {
            return ConfigResult::Failure("", "onvif unavailable");
        }
        if (!onvif_->ApplyOptions(BuildOnvifOptions(next_config))) {
            return ConfigResult::Failure("", "apply onvif config failed");
        }
    }
    runtime_config_ = next_config;
    return ConfigResult::Success();
}

void ProtocolSubsystem::Stop() {
    DetachRuntimeConfigAttachments();
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
