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
        config_scope.verify = [this, scope](const ConfigJson &now,
                                            ConfigIssue *issue) {
            return VerifyProtocolConfigUpdate(scope, now, issue);
        };
        config_scope.apply = [this, scope](const ConfigJson &prev,
                                           const ConfigJson &now,
                                           ConfigIssue *issue) {
            return ApplyProtocolConfigUpdate(scope, prev, now, issue);
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
            root[item] = config_->Get(item);
        }
    }
    return LoadAppConfigFromRoot(root, next_config);
}

ConfigStatus ProtocolSubsystem::VerifyProtocolConfigUpdate(
    const std::string &scope,
    const ConfigJson &now,
    ConfigIssue *issue) {
    AppConfig next_config;
    if (!BuildNextAppConfig(scope, now, &next_config)) {
        if (issue != nullptr) {
            issue->field.clear();
            issue->reason = "invalid app config";
        }
        return ConfigStatus::kVerifyFailed;
    }
    return VerifyProtocolConfigUpdateScope(app_config_, next_config, scope,
                                           issue);
}

ConfigStatus ProtocolSubsystem::ApplyProtocolConfigUpdate(
    const std::string &scope,
    const ConfigJson &prev,
    const ConfigJson &now,
    ConfigIssue *issue) {
    (void)prev;
    AppConfig next_config;
    if (!BuildNextAppConfig(scope, now, &next_config)) {
        if (issue != nullptr) {
            issue->field.clear();
            issue->reason = "invalid app config";
        }
        return ConfigStatus::kApplyFailed;
    }
    const ConfigStatus verify_status =
        VerifyProtocolConfigUpdateScope(app_config_, next_config, scope, issue);
    if (verify_status != ConfigStatus::kOk) {
        return verify_status;
    }

    if (scope == "rtsp" &&
        IsRtspConfigChanged(app_config_, next_config)) {
        if (rtsp_ == nullptr) {
            if (issue != nullptr) {
                issue->field.clear();
                issue->reason = "rtsp unavailable";
            }
            return ConfigStatus::kApplyFailed;
        }
        if (!rtsp_->ApplyOptions(BuildRtspOptions(next_config))) {
            if (issue != nullptr) {
                issue->field.clear();
                issue->reason = "apply rtsp config failed";
            }
            return ConfigStatus::kApplyFailed;
        }
    }
    if (scope == "webrtc" &&
        IsWebrtcConfigChanged(app_config_, next_config)) {
        if (webrtc_ == nullptr) {
            if (issue != nullptr) {
                issue->field.clear();
                issue->reason = "webrtc unavailable";
            }
            return ConfigStatus::kApplyFailed;
        }
        ProtocolStartupRefs refs;
        refs.device.network = network_;
        refs.net_io = net_io_.get();
        refs.rtsp = rtsp_.get();
        refs.onvif = onvif_.get();
        refs.webrtc = webrtc_.get();
        const WebrtcOptions options = BuildWebrtcOptions(next_config, refs);
        if (!webrtc_->ApplyOptions(options)) {
            if (issue != nullptr) {
                issue->field.clear();
                issue->reason = "apply webrtc config failed";
            }
            return ConfigStatus::kApplyFailed;
        }
    }
    if (scope == "onvif" &&
        IsOnvifConfigChanged(app_config_, next_config)) {
        if (onvif_ == nullptr) {
            if (issue != nullptr) {
                issue->field.clear();
                issue->reason = "onvif unavailable";
            }
            return ConfigStatus::kApplyFailed;
        }
        if (!onvif_->ApplyOptions(BuildOnvifOptions(next_config))) {
            if (issue != nullptr) {
                issue->field.clear();
                issue->reason = "apply onvif config failed";
            }
            return ConfigStatus::kApplyFailed;
        }
    }
    app_config_ = next_config;
    return ConfigStatus::kOk;
}

}  // namespace live_stream
