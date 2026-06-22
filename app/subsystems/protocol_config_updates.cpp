#include "subsystems/protocol_subsystem.h"

#include <string>

#include "config/protocol_config_update.h"
#include "subsystems/protocol_options.h"

namespace live_stream {

bool ProtocolSubsystem::InstallConfigUpdateAttachments() {
    if (config_ == nullptr) {
        return false;
    }
    const char *scopes[] = {"http", "rtsp", "webrtc", "onvif"};
    for (const char *scope : scopes) {
        ConfigAttachment attachment;
        attachment.validate = [this, scope](const ConfigJson &value) {
            return ValidateProtocolConfigUpdate(scope, value);
        };
        attachment.apply = [this, scope](const ConfigJson &value) {
            return ApplyProtocolConfigUpdate(scope, value);
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

void ProtocolSubsystem::DetachConfigUpdateAttachments() {
    if (config_ == nullptr) {
        return;
    }
    static_cast<void>(config_->DetachConfig("http"));
    static_cast<void>(config_->DetachConfig("rtsp"));
    static_cast<void>(config_->DetachConfig("webrtc"));
    static_cast<void>(config_->DetachConfig("onvif"));
    config_ = nullptr;
    network_ = nullptr;
}

bool ProtocolSubsystem::BuildNextAppConfig(
    const std::string &scope,
    const ConfigJson &value,
    AppConfig *next_config) const {
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
    return LoadAppConfigFromRoot(root, next_config);
}

ConfigResult ProtocolSubsystem::ValidateProtocolConfigUpdate(
    const std::string &scope,
    const ConfigJson &value) {
    AppConfig next_config;
    if (!BuildNextAppConfig(scope, value, &next_config)) {
        return ConfigResult::Failure("", "invalid app config");
    }
    return ValidateProtocolConfigUpdateScope(app_config_, next_config, scope);
}

ConfigResult ProtocolSubsystem::ApplyProtocolConfigUpdate(
    const std::string &scope,
    const ConfigJson &value) {
    AppConfig next_config;
    if (!BuildNextAppConfig(scope, value, &next_config)) {
        return ConfigResult::Failure("", "invalid app config");
    }
    const ConfigResult validate_result =
        ValidateProtocolConfigUpdateScope(app_config_, next_config, scope);
    if (!validate_result.ok) {
        return validate_result;
    }

    if (scope == "rtsp" &&
        IsRtspConfigChanged(app_config_, next_config)) {
        if (rtsp_ == nullptr) {
            return ConfigResult::Failure("", "rtsp unavailable");
        }
        if (!rtsp_->ApplyOptions(BuildRtspOptions(next_config))) {
            return ConfigResult::Failure("", "apply rtsp config failed");
        }
    }
    if (scope == "webrtc" &&
        IsWebrtcConfigChanged(app_config_, next_config)) {
        if (webrtc_ == nullptr) {
            return ConfigResult::Failure("", "webrtc unavailable");
        }
        ProtocolStartupRefs refs;
        refs.device.network = network_;
        refs.net_engine = net_engine_.get();
        refs.rtsp = rtsp_.get();
        refs.onvif = onvif_.get();
        refs.webrtc = webrtc_.get();
        const WebrtcOptions options = BuildWebrtcOptions(next_config, refs);
        if (!webrtc_->ApplyOptions(options)) {
            return ConfigResult::Failure("", "apply webrtc config failed");
        }
    }
    if (scope == "onvif" &&
        IsOnvifConfigChanged(app_config_, next_config)) {
        if (onvif_ == nullptr) {
            return ConfigResult::Failure("", "onvif unavailable");
        }
        if (!onvif_->ApplyOptions(BuildOnvifOptions(next_config))) {
            return ConfigResult::Failure("", "apply onvif config failed");
        }
    }
    app_config_ = next_config;
    return ConfigResult::Success();
}

}  // namespace live_stream
