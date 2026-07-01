#include "subsystems/protocol_subsystem.h"

#include "http_dependencies.h"
#include "infra/log.h"
#include "runtime.h"
#include "service_registry.h"
#include "subsystems/foundation_subsystem.h"
#include "subsystems/device_subsystem.h"
#include "subsystems/media_subsystem.h"
#include "subsystems/protocol_options.h"

namespace live_stream {
namespace {

event::Loop *RequireNetLoop(INetIo *net_io,
                            const char *owner_protocol) {
    const char *protocol =
        owner_protocol != nullptr ? owner_protocol : "unknown";
    if (net_io == nullptr) {
        Error("app", "Pick net loop failed protocol=%s", protocol);
        return nullptr;
    }

    event::Loop *loop = net_io->PickLoop();
    if (loop != nullptr) {
        return loop;
    }
    loop = net_io->DefaultLoop();
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
                              FoundationSubsystem &foundation_subsystem,
                              const DeviceRefs &device_refs,
                              const MediaRefs &media_refs) {
    if (started_) {
        return true;
    }

    ProtocolStartupRefs refs;
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

    const NetIoOptions net_options =
        BuildNetIoOptions(net_callback_loop_.get());
    net_io_ = CreateNetIo(net_options);
    if (!net_io_ || !net_io_->Start()) {
        Error("app", "Start net io failed");
        Stop();
        return false;
    }
    if (!Runtime::InstallNetIo(net_io_.get())) {
        Error("app", "Install runtime net io failed");
        Stop();
        return false;
    }
    refs.net_io = net_io_.get();
    refs.rtsp_loop = RequireNetLoop(refs.net_io, "rtsp");
    refs.webrtc_loop = RequireNetLoop(refs.net_io, "webrtc");
    refs.onvif_loop = RequireNetLoop(refs.net_io, "onvif");
    refs.http_loop = RequireNetLoop(refs.net_io, "http");
    if (refs.rtsp_loop == nullptr ||
        refs.webrtc_loop == nullptr ||
        refs.onvif_loop == nullptr ||
        refs.http_loop == nullptr) {
        Stop();
        return false;
    }

    const RtspOptions rtsp_options = BuildRtspOptions(app_config);
    rtsp_ = CreateRtsp(rtsp_options, refs.rtsp_loop);
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
        if (!ServiceRegistry::RegisterRtsp(rtsp_.get())) {
            Error("app", "Register RTSP readonly service failed");
            Stop();
            return false;
        }
        const RtspListenAddress rtsp_address = rtsp_->LocalAddress();
        Info("app", "RTSP listening %s:%u",
             rtsp_address.ip.c_str(),
             static_cast<unsigned>(rtsp_address.port));
    }

    const WebrtcOptions webrtc_options =
        BuildWebrtcOptions(app_config, refs);
    webrtc_ = CreateWebrtc(webrtc_options, refs.webrtc_loop);
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
        if (!ServiceRegistry::RegisterWebrtc(webrtc_.get())) {
            Error("app", "Register WebRTC readonly service failed");
            Stop();
            return false;
        }
    }

    const OnvifServerOptions onvif_options = BuildOnvifOptions(app_config);
    const OnvifServerDependencies onvif_dependencies =
        BuildOnvifDependencies(refs, foundation_subsystem);
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
        if (!ServiceRegistry::RegisterOnvif(onvif_.get())) {
            Error("app", "Register ONVIF readonly service failed");
            Stop();
            return false;
        }
        Info("app", "ONVIF started listen=%s:%u discovery=%u",
             app_config.listen_ip.c_str(),
             static_cast<unsigned>(app_config.onvif_device_port),
             static_cast<unsigned>(app_config.onvif_discovery_port));
    }

    const HttpOptions http_options = BuildHttpOptions(app_config);
    const HttpDependencies http_dependencies =
        BuildHttpDependencies(refs, foundation_subsystem);
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
    if (!ServiceRegistry::RegisterHttp(http_.get())) {
        Error("app", "Register HTTP readonly service failed");
        Stop();
        return false;
    }
    Info("app", "HTTP listening %s:%u root=%s",
         http_address.ip.c_str(),
         static_cast<unsigned>(http_address.port),
         app_config.static_root.c_str());

    const NetStatOptions net_stat_options =
        BuildNetStatOptions();
    net_stat_ =
        CreateNetStat(net_stat_options);
    if (!net_stat_ || !net_stat_->Start()) {
        Error("app", "Start net_stat failed");
        Stop();
        return false;
    }

    config_ = foundation_subsystem.config();
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
    ServiceRegistry::UnregisterHttp(http_.get());
    ServiceRegistry::UnregisterOnvif(onvif_.get());
    ServiceRegistry::UnregisterWebrtc(webrtc_.get());
    ServiceRegistry::UnregisterRtsp(rtsp_.get());
    if (net_stat_) {
        net_stat_->Stop();
    }
    if (http_) {
        http_->Stop();
    }
    if (onvif_) {
        onvif_->Stop();
    }
    if (webrtc_) {
        webrtc_->Stop();
    }
    if (rtsp_) {
        rtsp_->Stop();
    }
    if (net_io_) {
        net_io_->Stop();
        Runtime::ClearNetIo(net_io_.get());
        net_io_.reset();
    }
    if (net_callback_loop_) {
        net_callback_loop_->Stop(event::StopMode::kDiscard);
    }
    http_.reset();
    net_stat_.reset();
    onvif_.reset();
    webrtc_.reset();
    rtsp_.reset();
    ServiceRegistry::Clear();
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
