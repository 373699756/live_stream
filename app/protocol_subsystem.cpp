#include "protocol_subsystem.h"

#include <string>

#include "core_subsystem.h"
#include "device_subsystem.h"
#include "infra/log.h"
#include "media_subsystem.h"
#include "protocol_options.h"
#include "protocol_runtime_config.h"

namespace live_stream {

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
    const HttpConsoleDependencies http_dependencies =
        BuildHttpConsoleDependencies(refs);
    http_ = CreateHttpConsole(http_options, http_dependencies);
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
