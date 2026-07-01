#include "subsystems/protocol_subsystem.h"

#include "infra/log.h"
#include "runtime.h"
#include "service_registry.h"
#include "subsystems/foundation_subsystem.h"
#include "subsystems/device_subsystem.h"
#include "subsystems/media_subsystem.h"
#include "subsystems/protocol_options.h"

namespace live_stream {
namespace {

event::Loop *RequireSocketLoop(ISocketIo *socket_io,
                            const char *owner_protocol) {
    const char *protocol =
        owner_protocol != nullptr ? owner_protocol : "unknown";
    if (socket_io == nullptr) {
        Error("app", "Pick socket loop failed protocol=%s", protocol);
        return nullptr;
    }

    event::Loop *loop = socket_io->PickLoop();
    if (loop != nullptr) {
        return loop;
    }
    loop = socket_io->DefaultLoop();
    if (loop == nullptr) {
        Error("app", "Pick socket loop failed protocol=%s", protocol);
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

    if (!socket_callback_loop_) {
        socket_callback_loop_.reset(new event::Loop());
    }
    const event::LoopOptions callback_loop_options =
        BuildSocketCallbackOptions();
    if (!socket_callback_loop_->Start(callback_loop_options)) {
        Error("app", "Start socket callback loop failed");
        Stop();
        return false;
    }

    const SocketIoOptions socket_options =
        BuildSocketIoOptions(socket_callback_loop_.get());
    socket_io_ = CreateSocketIo(socket_options);
    if (!socket_io_ || !socket_io_->Start()) {
        Error("app", "Start socket io failed");
        Stop();
        return false;
    }
    if (!Runtime::InstallSocketIo(socket_io_.get())) {
        Error("app", "Install runtime socket io failed");
        Stop();
        return false;
    }
    refs.socket_io = socket_io_.get();
    refs.rtsp_loop = RequireSocketLoop(refs.socket_io, "rtsp");
    refs.webrtc_loop = RequireSocketLoop(refs.socket_io, "webrtc");
    refs.onvif_loop = RequireSocketLoop(refs.socket_io, "onvif");
    refs.http_loop = RequireSocketLoop(refs.socket_io, "http");
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
    onvif_ = CreateOnvifServer(onvif_options, refs.onvif_loop,
                               refs.device.system, refs.device.time,
                               refs.media.device);
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
    http_ = CreateHttp(http_options, refs.http_loop, refs.device.network,
                       refs.device.time, refs.device.alarm,
                       refs.device.upgrade, refs.device.system,
                       refs.media.ai, refs.media.device, refs.webrtc);
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
        CreateNetStat(net_stat_options, socket_io_.get(),
                      Runtime::EventCenter());
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
    if (socket_io_) {
        socket_io_->Stop();
        Runtime::ClearSocketIo(socket_io_.get());
        socket_io_.reset();
    }
    if (socket_callback_loop_) {
        socket_callback_loop_->Stop(event::StopMode::kDiscard);
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
