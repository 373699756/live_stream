#include "subsystems/protocol_subsystem.h"

#include "http_dependencies.h"
#include "infra/log.h"
#include "subsystems/core_subsystem.h"
#include "subsystems/device_subsystem.h"
#include "subsystems/media_subsystem.h"
#include "subsystems/protocol_options.h"

namespace live_stream {
namespace {

event::Loop *RequireNetLoop(INetEngine *net_engine,
                            const char *owner_protocol) {
    const char *protocol =
        owner_protocol != nullptr ? owner_protocol : "unknown";
    if (net_engine == nullptr) {
        Error("app", "Pick net loop failed protocol=%s", protocol);
        return nullptr;
    }

    event::Loop *loop = net_engine->PickLoop();
    if (loop != nullptr) {
        return loop;
    }
    loop = net_engine->DefaultLoop();
    if (loop == nullptr) {
        Error("app", "Pick net loop failed protocol=%s", protocol);
    }
    return loop;
}

}  // namespace

ProtocolSubsystem &ProtocolSubsystem::Get() {
    static ProtocolSubsystem subsystem;
    return subsystem;
}

bool ProtocolSubsystem::Start(const AppConfig &app_config,
                              CoreSubsystem &core_subsystem,
                              const DeviceRefs &device_refs,
                              const MediaRefs &media_refs) {
    if (started_) {
        return true;
    }

    ProtocolStartupRefs refs;
    refs.core = &core_subsystem;
    refs.device = device_refs;
    refs.media = media_refs;

    if (!net_callback_loop_) {
        net_callback_loop_.reset(new event::Loop());
    }
    const event::LoopOptions callback_loop_options =
        BuildNetCallbackOptions();
    if (!net_callback_loop_->Start(callback_loop_options)) {
        Error("app", "Start net callback loop failed");
        Stop();
        return false;
    }

    const NetEngineOptions net_options =
        BuildNetEngineOptions(net_callback_loop_.get());
    net_engine_ = CreateNetEngine(net_options);
    if (!net_engine_ || !net_engine_->Start()) {
        Error("app", "Start net engine failed");
        Stop();
        return false;
    }
    refs.net_engine = net_engine_.get();
    refs.rtsp_loop = RequireNetLoop(refs.net_engine, "rtsp");
    refs.webrtc_loop = RequireNetLoop(refs.net_engine, "webrtc");
    refs.onvif_loop = RequireNetLoop(refs.net_engine, "onvif");
    refs.http_loop = RequireNetLoop(refs.net_engine, "http");
    if (refs.rtsp_loop == nullptr ||
        refs.webrtc_loop == nullptr ||
        refs.onvif_loop == nullptr ||
        refs.http_loop == nullptr) {
        Stop();
        return false;
    }

    const RtspOptions rtsp_options = BuildRtspOptions(app_config);
    const RtspDependencies rtsp_dependencies = BuildRtspDependencies(refs);
    rtsp_ = CreateRtsp(rtsp_options, rtsp_dependencies);
    if (!rtsp_ || !rtsp_->Start()) {
        Error("app",
              "Start rtsp failed, continue without RTSP: "
              "listen=%s:%u",
              app_config.listen_ip.c_str(),
              static_cast<unsigned>(app_config.rtsp_port));
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
        BuildWebrtcOptions(app_config, refs);
    const WebrtcDependencies webrtc_dependencies =
        BuildWebrtcDependencies(refs);
    webrtc_ = CreateWebrtc(webrtc_options, webrtc_dependencies);
    if (!webrtc_ || !webrtc_->Start()) {
        Error(
            "app",
            "Start webrtc failed, continue without WebRTC: "
            "enabled=%d base=%u",
            app_config.webrtc_enabled ? 1 : 0,
            static_cast<unsigned>(app_config.webrtc_local_port_base));
        if (webrtc_) {
            webrtc_->Stop();
        }
        webrtc_.reset();
        refs.webrtc = nullptr;
    } else {
        refs.webrtc = webrtc_.get();
    }

    const OnvifServerOptions onvif_options = BuildOnvifOptions(app_config);
    const OnvifServerDependencies onvif_dependencies =
        BuildOnvifDependencies(refs);
    onvif_ = CreateOnvifServer(onvif_options, onvif_dependencies);
    if (!onvif_ || !onvif_->Start()) {
        Error("app",
              "Start onvif failed, continue without ONVIF: "
              "device_port=%u",
              static_cast<unsigned>(app_config.onvif_device_port));
        if (onvif_) {
            onvif_->Stop();
        }
        onvif_.reset();
        refs.onvif = nullptr;
    } else {
        refs.onvif = onvif_.get();
        Info("app", "ONVIF started listen=%s:%u discovery=%u",
             app_config.listen_ip.c_str(),
             static_cast<unsigned>(app_config.onvif_device_port),
             static_cast<unsigned>(app_config.onvif_discovery_port));
    }

    const HttpOptions http_options = BuildHttpOptions(app_config);
    const HttpDependencies http_dependencies = BuildHttpDependencies(refs);
    http_ = CreateHttp(http_options, http_dependencies);
    if (!http_ || !http_->Start()) {
        Error("app", "Start http failed: listen=%s:%u root=%s",
              app_config.listen_ip.c_str(),
              static_cast<unsigned>(app_config.http_port),
              app_config.static_root.c_str());
        Stop();
        return false;
    }
    const HttpListenAddress http_address = http_->LocalAddress();
    Info("app", "HTTP listening %s:%u root=%s",
         http_address.ip.c_str(),
         static_cast<unsigned>(http_address.port),
         app_config.static_root.c_str());

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
    network_ = device_refs.network;
    app_config_ = app_config;
    if (!InstallConfigUpdateScopes()) {
        Error("app", "Install protocol config update attachments failed");
        Stop();
        return false;
    }

    started_ = true;
    return true;
}

void ProtocolSubsystem::Stop() {
    RemoveConfigUpdateScopes();
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
    if (net_callback_loop_) {
        Info("app", "Stop net callback loop begin");
        net_callback_loop_->Stop(event::StopMode::kDiscard);
        Info("app", "Stop net callback loop done");
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
