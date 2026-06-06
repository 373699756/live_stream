#include "protocol_subsystem.h"

#include <string>

#include "core_services.h"
#include "device_subsystem.h"
#include "http_console_service.h"
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

struct ProtocolRuntimeRefs {
    CoreServices *core = nullptr;
    DeviceRefs device;
    MediaRefs media;
    NetEngine *net_engine = nullptr;
    IRtspService *rtsp_service = nullptr;
    IOnvifService *onvif_service = nullptr;
    IWebrtcService *webrtc_service = nullptr;
    IMediaSourceService *media_source_service = nullptr;
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

RtspServiceOptions BuildRtspOptions(const AppRuntimeConfig &runtime_config) {
    RtspServiceOptions options;
    options.listen_ip = runtime_config.listen_ip;
    options.listen_port = runtime_config.rtsp_port;
    options.max_sessions = runtime_config.rtsp_max_sessions;
    options.enable_auth = runtime_config.rtsp_auth_required;
    options.main_stream_codec = runtime_config.rtsp_main_codec;
    options.sub_stream_codec = runtime_config.rtsp_sub_codec;
    return options;
}

RtspServiceDependencies BuildRtspDependencies(const ProtocolRuntimeRefs &refs) {
    RtspServiceDependencies dependencies;
    dependencies.net_engine = refs.net_engine;
    dependencies.auth_service = refs.core != nullptr ? refs.core->auth() : nullptr;
    dependencies.event_service = refs.core != nullptr ? refs.core->event() : nullptr;
    dependencies.media_source = refs.media_source_service;
    return dependencies;
}

WebrtcServiceOptions BuildWebrtcOptions(
    const AppRuntimeConfig &runtime_config) {
    WebrtcServiceOptions options;
    options.enabled = runtime_config.webrtc_enabled;
    options.local_port_base = runtime_config.webrtc_local_port_base;
    options.max_peers = runtime_config.webrtc_max_peers;
    options.prefer_tcp = runtime_config.webrtc_prefer_tcp;
    options.public_ip = runtime_config.advertise_host;
    options.ice_servers = runtime_config.webrtc_ice_servers;
    return options;
}

WebrtcServiceDependencies BuildWebrtcDependencies(
    const ProtocolRuntimeRefs &refs) {
    WebrtcServiceDependencies dependencies;
    dependencies.net_engine = refs.net_engine;
    dependencies.media_source = refs.media_source_service;
    dependencies.use_fake_engine = false;
    return dependencies;
}

MediaSourceServiceOptions BuildMediaSourceOptions() {
    MediaSourceServiceOptions options;
    return options;
}

MediaSourceServiceDependencies BuildMediaSourceDependencies(
    const ProtocolRuntimeRefs &refs) {
    MediaSourceServiceDependencies dependencies;
    dependencies.media_service = refs.media.media;
    return dependencies;
}

OnvifServiceOptions BuildOnvifOptions(const AppRuntimeConfig &runtime_config) {
    OnvifServiceOptions options;
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
    options.rtsp_port = runtime_config.rtsp_port;
    options.http_port = runtime_config.http_port;
    return options;
}

OnvifServiceDependencies BuildOnvifDependencies(
    const ProtocolRuntimeRefs &refs) {
    OnvifServiceDependencies dependencies;
    dependencies.net_engine = refs.net_engine;
    dependencies.auth_service = refs.core != nullptr ? refs.core->auth() : nullptr;
    dependencies.config_service = refs.core != nullptr ? refs.core->config() : nullptr;
    dependencies.event_service = refs.core != nullptr ? refs.core->event() : nullptr;
    dependencies.system_service = refs.device.system;
    dependencies.time_service = refs.device.time;
    dependencies.media_service = refs.media.media;
    return dependencies;
}

HttpServiceOptions BuildHttpOptions(const AppRuntimeConfig &runtime_config) {
    HttpServiceOptions options;
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

}  // namespace

ProtocolSubsystem &ProtocolSubsystem::Get() {
    static ProtocolSubsystem subsystem;
    return subsystem;
}

bool ProtocolSubsystem::Start(const AppRuntimeConfig &runtime_config,
                              CoreServices &core_services,
                              const DeviceRefs &device_refs,
                              const MediaRefs &media_refs) {
    if (started_) {
        return true;
    }

    ProtocolRuntimeRefs refs;
    refs.core = &core_services;
    refs.device = device_refs;
    refs.media = media_refs;

    if (!net_callback_executor_) {
        net_callback_executor_.reset(new infra::Executor());
    }
    const infra::ExecutorOptions callback_executor_options =
        BuildNetCallbackExecutorOptions();
    if (!net_callback_executor_->Start(callback_executor_options)) {
        INFRA_LOG_ERROR("app", "Start net callback executor failed");
        Stop();
        return false;
    }

    const NetEngineOptions net_options =
        BuildNetEngineOptions(net_callback_executor_.get());
    net_engine_ = CreateNetEngine(net_options);
    if (!net_engine_ || !net_engine_->Start()) {
        INFRA_LOG_ERROR("app", "Start net engine failed");
        Stop();
        return false;
    }
    refs.net_engine = net_engine_.get();

    const MediaSourceServiceOptions media_source_options =
        BuildMediaSourceOptions();
    const MediaSourceServiceDependencies media_source_dependencies =
        BuildMediaSourceDependencies(refs);
    media_source_ =
        CreateMediaSourceService(media_source_options,
                                 media_source_dependencies);
    if (!media_source_ || !media_source_->Start()) {
        INFRA_LOG_ERROR("app", "Start media source service failed");
        Stop();
        return false;
    }
    refs.media_source_service = media_source_.get();

    const RtspServiceOptions rtsp_options = BuildRtspOptions(runtime_config);
    const RtspServiceDependencies rtsp_dependencies = BuildRtspDependencies(refs);
    rtsp_ = CreateRtspService(rtsp_options, rtsp_dependencies);
    if (!rtsp_ || !rtsp_->Start()) {
        INFRA_LOG_ERROR("app",
                        "Start rtsp service failed, continue without RTSP: "
                        "listen=%s:%u",
                        runtime_config.listen_ip.c_str(),
                        static_cast<unsigned>(runtime_config.rtsp_port));
        if (rtsp_) {
            rtsp_->Stop();
        }
        rtsp_.reset();
        refs.rtsp_service = nullptr;
    } else {
        refs.rtsp_service = rtsp_.get();
        const RtspListenAddress rtsp_address = rtsp_->LocalAddress();
        INFRA_LOG_INFO("app", "RTSP service listening %s:%u",
                       rtsp_address.ip.c_str(),
                       static_cast<unsigned>(rtsp_address.port));
    }

    const WebrtcServiceOptions webrtc_options =
        BuildWebrtcOptions(runtime_config);
    const WebrtcServiceDependencies webrtc_dependencies =
        BuildWebrtcDependencies(refs);
    webrtc_ = CreateWebrtcService(webrtc_options, webrtc_dependencies);
    if (!webrtc_ || !webrtc_->Start()) {
        INFRA_LOG_ERROR(
            "app",
            "Start webrtc service failed, continue without WebRTC: "
            "enabled=%d base=%u",
            runtime_config.webrtc_enabled ? 1 : 0,
            static_cast<unsigned>(runtime_config.webrtc_local_port_base));
        if (webrtc_) {
            webrtc_->Stop();
        }
        webrtc_.reset();
        refs.webrtc_service = nullptr;
    } else {
        refs.webrtc_service = webrtc_.get();
    }

    const OnvifServiceOptions onvif_options = BuildOnvifOptions(runtime_config);
    const OnvifServiceDependencies onvif_dependencies =
        BuildOnvifDependencies(refs);
    onvif_ = CreateOnvifService(onvif_options, onvif_dependencies);
    if (!onvif_ || !onvif_->Start()) {
        INFRA_LOG_ERROR("app",
                        "Start onvif service failed, continue without ONVIF: "
                        "device_port=%u",
                        static_cast<unsigned>(runtime_config.onvif_device_port));
        if (onvif_) {
            onvif_->Stop();
        }
        onvif_.reset();
        refs.onvif_service = nullptr;
    } else {
        refs.onvif_service = onvif_.get();
        INFRA_LOG_INFO("app", "ONVIF service started listen=%s:%u discovery=%u",
                       runtime_config.listen_ip.c_str(),
                       static_cast<unsigned>(runtime_config.onvif_device_port),
                       static_cast<unsigned>(runtime_config.onvif_discovery_port));
    }

    const HttpServiceOptions http_options = BuildHttpOptions(runtime_config);
    http_ = CreateHttpConsoleService(
        http_options, refs.net_engine,
        refs.core != nullptr ? refs.core->auth() : nullptr,
        refs.core != nullptr ? refs.core->logger() : nullptr,
        refs.core != nullptr ? refs.core->config() : nullptr,
        refs.device.network, refs.device.time, refs.device.alarm,
        refs.device.upgrade, refs.device.system, refs.rtsp_service,
        refs.onvif_service, refs.media.ai, refs.media.media,
        refs.media.snapshot, refs.webrtc_service, refs.media_source_service,
        refs.media_source_service, refs.media_source_service);
    if (!http_ || !http_->Start()) {
        INFRA_LOG_ERROR("app", "Start http service failed: listen=%s:%u root=%s",
                        runtime_config.listen_ip.c_str(),
                        static_cast<unsigned>(runtime_config.http_port),
                        runtime_config.static_root.c_str());
        Stop();
        return false;
    }
    const HttpListenAddress http_address = http_->LocalAddress();
    INFRA_LOG_INFO("app", "HTTP service listening %s:%u root=%s",
                   http_address.ip.c_str(),
                   static_cast<unsigned>(http_address.port),
                   runtime_config.static_root.c_str());

    started_ = true;
    return true;
}

void ProtocolSubsystem::Stop() {
    if (http_) {
        INFRA_LOG_INFO("app", "Stop HTTP service begin");
        http_->Stop();
        INFRA_LOG_INFO("app", "Stop HTTP service done");
    }
    if (onvif_) {
        INFRA_LOG_INFO("app", "Stop ONVIF service begin");
        onvif_->Stop();
        INFRA_LOG_INFO("app", "Stop ONVIF service done");
    }
    if (webrtc_) {
        INFRA_LOG_INFO("app", "Stop WebRTC service begin");
        webrtc_->Stop();
        INFRA_LOG_INFO("app", "Stop WebRTC service done");
    }
    if (rtsp_) {
        INFRA_LOG_INFO("app", "Stop RTSP service begin");
        rtsp_->Stop();
        INFRA_LOG_INFO("app", "Stop RTSP service done");
    }
    if (media_source_) {
        INFRA_LOG_INFO("app", "Stop media source service begin");
        media_source_->Stop();
        INFRA_LOG_INFO("app", "Stop media source service done");
    }
    if (net_engine_) {
        INFRA_LOG_INFO("app", "Stop net engine begin");
        net_engine_->Stop();
        net_engine_.reset();
        INFRA_LOG_INFO("app", "Stop net engine done");
    }
    if (net_callback_executor_) {
        INFRA_LOG_INFO("app", "Stop net callback executor begin");
        net_callback_executor_->Stop(infra::StopMode::kDiscard);
        INFRA_LOG_INFO("app", "Stop net callback executor done");
    }
    http_.reset();
    onvif_.reset();
    media_source_.reset();
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
