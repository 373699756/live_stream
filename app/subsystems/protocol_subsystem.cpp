#include "subsystems/protocol_subsystem.h"

#include "http_dependencies.h"
#include "infra/log.h"
#include "subsystems/core_subsystem.h"
#include "subsystems/device_subsystem.h"
#include "subsystems/media_subsystem.h"
#include "subsystems/protocol_options.h"

namespace live_stream {
namespace {

INetExecutor *RequireNetExecutor(INetEngine *net_engine,
                                 const char *owner_protocol) {
    const char *protocol =
        owner_protocol != nullptr ? owner_protocol : "unknown";
    if (net_engine == nullptr) {
        Error("app", "Pick net executor failed protocol=%s", protocol);
        return nullptr;
    }

    INetExecutor *executor = net_engine->PickExecutor();
    if (executor != nullptr) {
        return executor;
    }
    executor = net_engine->DefaultExecutor();
    if (executor == nullptr) {
        Error("app", "Pick net executor failed protocol=%s", protocol);
    }
    return executor;
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
        BuildNetCallbackOptions();
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
    refs.rtsp_executor = RequireNetExecutor(refs.net_engine, "rtsp");
    refs.webrtc_executor = RequireNetExecutor(refs.net_engine, "webrtc");
    refs.onvif_executor = RequireNetExecutor(refs.net_engine, "onvif");
    refs.http_executor = RequireNetExecutor(refs.net_engine, "http");
    if (refs.rtsp_executor == nullptr ||
        refs.webrtc_executor == nullptr ||
        refs.onvif_executor == nullptr ||
        refs.http_executor == nullptr) {
        Stop();
        return false;
    }

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
    const HttpDependencies http_dependencies = BuildHttpDependencies(refs);
    http_ = CreateHttp(http_options, http_dependencies);
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

    const NetStatOptions net_stat_options =
        BuildNetStatOptions();
    const NetStatDependencies net_stat_dependencies =
        BuildNetStatDependencies(refs);
    net_stat_ =
        CreateNetStat(net_stat_options, net_stat_dependencies);
    if (!net_stat_ || !net_stat_->Start()) {
        Error("app", "Start net_stat failed");
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

void ProtocolSubsystem::Stop() {
    DetachRuntimeConfigAttachments();
    if (net_stat_) {
        Info("app", "Stop net_stat begin");
        net_stat_->Stop();
        Info("app", "Stop net_stat done");
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
    net_stat_.reset();
    onvif_.reset();
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
