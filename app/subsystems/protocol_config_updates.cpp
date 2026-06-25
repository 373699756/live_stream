#include "subsystems/protocol_subsystem.h"

#include <string>

#include "config/protocol_config_update.h"
#include "subsystems/protocol_options.h"

namespace live_stream {

bool ProtocolSubsystem::InstallConfigUpdateScopes() {
    if (config_ == nullptr) {
        return false;
    }
    const char *scopes[] = {"http", "rtsp", "webrtc", "onvif"};
    for (const char *scope : scopes) {
        ConfigScope config_scope;
        config_scope.verify = [this, scope](const Json &now,
                                            ConfigError *error) {
            return VerifyProtocolConfigUpdate(scope, now, error);
        };
        config_scope.apply = [this, scope](const Json &prev,
                                           const Json &now,
                                           ConfigError *error) {
            return ApplyProtocolConfigUpdate(scope, prev, now, error);
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

ConfigCode ProtocolSubsystem::VerifyProtocolConfigUpdate(
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
    return VerifyProtocolConfigUpdateScope(app_config_, next_config, scope,
                                           error);
}

ConfigCode ProtocolSubsystem::ApplyProtocolConfigUpdate(
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
        VerifyProtocolConfigUpdateScope(app_config_, next_config, scope, error);
    if (verify_code != ConfigCode::kOk) {
        return verify_code;
    }

    if (scope == "rtsp" &&
        IsRtspConfigChanged(app_config_, next_config)) {
        if (rtsp_ == nullptr) {
            if (error != nullptr) {
                error->field.clear();
                error->message = "rtsp unavailable";
            }
            return ConfigCode::kApply;
        }
        if (!rtsp_->ApplyOptions(BuildRtspOptions(next_config))) {
            if (error != nullptr) {
                error->field.clear();
                error->message = "apply rtsp config failed";
            }
            return ConfigCode::kApply;
        }
    }
    if (scope == "webrtc" &&
        IsWebrtcConfigChanged(app_config_, next_config)) {
        if (webrtc_ == nullptr) {
            if (error != nullptr) {
                error->field.clear();
                error->message = "webrtc unavailable";
            }
            return ConfigCode::kApply;
        }
        ProtocolStartupRefs refs;
        refs.device.network = network_;
        refs.net_io = net_io_.get();
        refs.rtsp = rtsp_.get();
        refs.onvif = onvif_.get();
        refs.webrtc = webrtc_.get();
        const WebrtcOptions options = BuildWebrtcOptions(next_config, refs);
        if (!webrtc_->ApplyOptions(options)) {
            if (error != nullptr) {
                error->field.clear();
                error->message = "apply webrtc config failed";
            }
            return ConfigCode::kApply;
        }
    }
    if (scope == "onvif" &&
        IsOnvifConfigChanged(app_config_, next_config)) {
        if (onvif_ == nullptr) {
            if (error != nullptr) {
                error->field.clear();
                error->message = "onvif unavailable";
            }
            return ConfigCode::kApply;
        }
        if (!onvif_->ApplyOptions(BuildOnvifOptions(next_config))) {
            if (error != nullptr) {
                error->field.clear();
                error->message = "apply onvif config failed";
            }
            return ConfigCode::kApply;
        }
    }
    app_config_ = next_config;
    return ConfigCode::kOk;
}

}  // namespace live_stream
