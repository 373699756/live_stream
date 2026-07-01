#include "subsystems/protocol_subsystem.h"

#include <string>

#include "config/protocol_config_change.h"
#include "subsystems/protocol_options.h"

namespace live_stream {
namespace {

ConfigCode RejectConfigApply(ConfigError *error, const char *message) {
    if (error != nullptr) {
        error->field.clear();
        error->message = message == nullptr ? "" : message;
    }
    return ConfigCode::kApply;
}

}  // namespace

bool ProtocolSubsystem::InstallConfigUpdateScopes() {
    if (config_ == nullptr) {
        return false;
    }
    const char *scopes[] = {"http", "rtsp", "webrtc", "onvif"};
    for (const char *scope : scopes) {
        ConfigScope config_scope;
        config_scope.verify = [this, scope](const Json &now,
                                            ConfigError *error) {
            return VerifyProtocolConfigChange(scope, now, error);
        };
        config_scope.apply = [this, scope](const Json &prev,
                                           const Json &now,
                                           ConfigError *error) {
            return ApplyProtocolConfigChange(scope, prev, now, error);
        };
        if (!config_->AddScope(scope, config_scope)) {
            for (const char *attached_scope : scopes) {
                if (std::string(attached_scope) == scope) {
                    break;
                }
                static_cast<void>(config_->RemoveScope(attached_scope));
            }
            return false;
        }
    }
    return true;
}

void ProtocolSubsystem::RemoveConfigUpdateScopes() {
    if (config_ == nullptr) {
        return;
    }
    static_cast<void>(config_->RemoveScope("http"));
    static_cast<void>(config_->RemoveScope("rtsp"));
    static_cast<void>(config_->RemoveScope("webrtc"));
    static_cast<void>(config_->RemoveScope("onvif"));
    config_ = nullptr;
    network_ = nullptr;
}

bool ProtocolSubsystem::BuildNextAppConfig(
    const std::string &scope,
    const Json &value,
    AppConfig &next_config) const {
    if (config_ == nullptr) {
        return false;
    }
    Json root = Json::object();
    const char *scopes[] = {"video", "network", "http", "rtsp",
                            "snapshot", "webrtc", "onvif"};
    for (const char *item : scopes) {
        if (scope == item) {
            root[item] = value;
        } else {
            root[item] = config_->Get(item);
        }
    }
    return LoadAppConfigFromRoot(root, &next_config);
}

ConfigCode ProtocolSubsystem::VerifyProtocolConfigChange(
    const std::string &scope,
    const Json &now,
    ConfigError *error) {
    AppConfig next_config;
    if (!BuildNextAppConfig(scope, now, next_config)) {
        if (error != nullptr) {
            error->field.clear();
            error->message = "invalid app config";
        }
        return ConfigCode::kVerify;
    }
    return VerifyProtocolConfigChangeScope(app_config_, next_config, scope,
                                           error);
}

ConfigCode ProtocolSubsystem::ApplyProtocolConfigChange(
    const std::string &scope,
    const Json &prev,
    const Json &now,
    ConfigError *error) {
    (void)prev;
    AppConfig next_config;
    if (!BuildNextAppConfig(scope, now, next_config)) {
        if (error != nullptr) {
            error->field.clear();
            error->message = "invalid app config";
        }
        return ConfigCode::kApply;
    }
    const ConfigCode verify_code =
        VerifyProtocolConfigChangeScope(app_config_, next_config, scope, error);
    if (verify_code != ConfigCode::kOk) {
        return verify_code;
    }

    ConfigCode apply_code = ConfigCode::kOk;
    if (scope == "rtsp" && IsRtspConfigChanged(app_config_, next_config)) {
        apply_code = ApplyRtspConfigChange(next_config, error);
    }
    if (scope == "webrtc" &&
        IsWebrtcConfigChanged(app_config_, next_config)) {
        apply_code = ApplyWebrtcConfigChange(next_config, error);
    }
    if (scope == "onvif" && IsOnvifConfigChanged(app_config_, next_config)) {
        apply_code = ApplyOnvifConfigChange(next_config, error);
    }
    if (apply_code != ConfigCode::kOk) {
        return apply_code;
    }
    app_config_ = next_config;
    return ConfigCode::kOk;
}

ConfigCode ProtocolSubsystem::ApplyRtspConfigChange(
    const AppConfig &next_config, ConfigError *error) {
    if (rtsp_ == nullptr) {
        return RejectConfigApply(error, "rtsp unavailable");
    }
    if (!rtsp_->ApplyOptions(BuildRtspOptions(next_config))) {
        return RejectConfigApply(error, "apply rtsp config failed");
    }
    return ConfigCode::kOk;
}

ConfigCode ProtocolSubsystem::ApplyWebrtcConfigChange(
    const AppConfig &next_config, ConfigError *error) {
    if (webrtc_ == nullptr) {
        return RejectConfigApply(error, "webrtc unavailable");
    }
    ProtocolStartupRefs refs;
    refs.device.network = network_;
    refs.socket_io = socket_io_.get();
    refs.rtsp = rtsp_.get();
    refs.onvif = onvif_.get();
    refs.webrtc = webrtc_.get();
    const WebrtcOptions options = BuildWebrtcOptions(next_config, refs);
    if (!webrtc_->ApplyOptions(options)) {
        return RejectConfigApply(error, "apply webrtc config failed");
    }
    return ConfigCode::kOk;
}

ConfigCode ProtocolSubsystem::ApplyOnvifConfigChange(
    const AppConfig &next_config, ConfigError *error) {
    if (onvif_ == nullptr) {
        return RejectConfigApply(error, "onvif unavailable");
    }
    if (!onvif_->ApplyOptions(BuildOnvifOptions(next_config))) {
        return RejectConfigApply(error, "apply onvif config failed");
    }
    return ConfigCode::kOk;
}

}  // namespace live_stream
