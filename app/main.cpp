#include "auth_service.h"
#include "config_service.h"
#include "event_service.h"
#include "http_service.h"
#include "infra/status.h"
#include "infra/log.h"
#include "infra/service.h"
#include "logger_service.h"
#include "media_service.h"
#include "netframe_service.h"
#include "network_service.h"
#include "osd_service.h"
#include "rtsp_service.h"
#include "snapshot_service.h"
#include "webrtc_service.h"

#include <csignal>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kBusinessConfigPath = "configs/business_config.json";
constexpr const char* kDefaultConfigPath = "configs/default_config.json";
constexpr const char* kAuthUsersPath = "configs/auth_users.json";
constexpr const char* kOperationLogPath = "build/runtime/operation.log";
constexpr const char* kStaticRoot = "www/dist";

volatile std::sig_atomic_t g_stop_requested = 0;

struct ManagedService {
    std::string name;
    std::unique_ptr<infra::IService> service;
    bool initialized = false;
    bool started = false;
};

const char* PendingServices[] = {
    "onvif_service",
    "alarm_service",
    "upgrade_service",
    "system_service",
    "time_service",
};

void HandleSignal(int) {
    g_stop_requested = 1;
}

live_stream::OperationAction MapAuthAction(
    live_stream::AuthAuditAction action) {
    switch (action) {
        case live_stream::AuthAuditAction::kLogin:
            return live_stream::OperationAction::kLogin;
        case live_stream::AuthAuditAction::kLogout:
            return live_stream::OperationAction::kLogout;
        case live_stream::AuthAuditAction::kAuthFailed:
            return live_stream::OperationAction::kAuthFailed;
        case live_stream::AuthAuditAction::kTokenExpired:
            return live_stream::OperationAction::kTokenExpired;
        case live_stream::AuthAuditAction::kPermissionDenied:
            return live_stream::OperationAction::kPermissionDenied;
    }
    return live_stream::OperationAction::kAuthFailed;
}

live_stream::OperationResult MapAuthResult(
    live_stream::AuthAuditResult result) {
    switch (result) {
        case live_stream::AuthAuditResult::kSuccess:
            return live_stream::OperationResult::kSuccess;
        case live_stream::AuthAuditResult::kFailed:
            return live_stream::OperationResult::kFailed;
        case live_stream::AuthAuditResult::kRejected:
            return live_stream::OperationResult::kRejected;
    }
    return live_stream::OperationResult::kFailed;
}

class AuthAuditToLoggerSink : public live_stream::IAuthAuditSink {
 public:
    explicit AuthAuditToLoggerSink(live_stream::ILoggerService* logger)
        : logger_(logger) {}

    infra::Status RecordAuthOperation(
        const live_stream::AuthAuditRecord& record) override {
        if (logger_ == nullptr) {
            return infra::Status::kInternalError;
        }

        live_stream::OperationRecord operation;
        operation.request_id = record.context.request_id;
        operation.user_name = record.user_name;
        operation.session_id = record.session_id;
        operation.client_ip = record.context.client_ip;
        operation.module = record.module;
        operation.action = MapAuthAction(record.action);
        operation.target = record.target;
        operation.result = MapAuthResult(record.result);
        operation.reason = record.reason;
        return logger_->RecordOperation(operation);
    }

 private:
    live_stream::ILoggerService* logger_;
};

std::unique_ptr<live_stream::ILoggerService> CreateLogger() {
    live_stream::LoggerServiceConfig config;
    config.operation_log_path = kOperationLogPath;
    return live_stream::CreateLoggerService(config);
}

std::unique_ptr<live_stream::IConfigService> CreateConfig() {
    live_stream::ConfigServiceOptions options;
    options.config_path = kBusinessConfigPath;
    options.default_config_path = kDefaultConfigPath;
    options.create_storage_if_missing = false;
    return live_stream::CreateConfigService(options);
}

std::unique_ptr<live_stream::IAuthService> CreateAuth(
    live_stream::IConfigService* config_service) {
    live_stream::AuthServiceOptions options;
    options.token_ttl_seconds = 30 * 60;
    options.max_sessions = 16;
    live_stream::AuthServiceDependencies dependencies;
    dependencies.config_service = config_service;
    return live_stream::CreateAuthService(
        options,
        dependencies,
        live_stream::CreateJsonAuthUserStore(kAuthUsersPath),
        live_stream::CreateSha256PasswordVerifier());
}

std::unique_ptr<live_stream::INetworkService> CreateNetwork(
    live_stream::IConfigService* config_service,
    live_stream::IEventService* event_service,
    live_stream::ILoggerService* logger_service) {
    live_stream::NetworkServiceOptions options;
    options.config_service = config_service;
    options.event_service = event_service;
    options.logger_service = logger_service;
    options.default_ifname = "eth0";
    return live_stream::CreateNetworkService(options);
}

std::unique_ptr<live_stream::MediaService> CreateMedia(
    live_stream::IConfigService* config_service) {
    live_stream::MediaServiceOptions options;
    options.config_service = config_service;
    return std::make_unique<live_stream::MediaService>(options);
}

std::unique_ptr<live_stream::OsdService> CreateOsd(
    live_stream::IConfigService* config_service) {
    live_stream::OsdServiceOptions options;
    options.config_service = config_service;
    return std::make_unique<live_stream::OsdService>(options);
}

std::unique_ptr<live_stream::SnapshotService> CreateSnapshot(
    live_stream::IConfigService* config_service) {
    live_stream::SnapshotServiceOptions options;
    options.config_service = config_service;
    return std::make_unique<live_stream::SnapshotService>(options);
}

std::unique_ptr<live_stream::IRtspService> CreateRtsp(
    live_stream::NetEngine* net_engine,
    live_stream::IAuthService* auth_service,
    live_stream::IEventService* event_service,
    live_stream::MediaService* media_service) {
    live_stream::RtspServiceOptions options;
    options.listen_port = 8554;
    options.enable_auth = true;
    live_stream::RtspServiceDependencies dependencies;
    dependencies.net_engine = net_engine;
    dependencies.auth_service = auth_service;
    dependencies.event_service = event_service;
    dependencies.media_service = media_service;
    return live_stream::CreateRtspService(options, dependencies);
}

std::unique_ptr<live_stream::IWebrtcService> CreateWebrtc(
    live_stream::NetEngine* net_engine,
    live_stream::MediaService* media_service) {
    live_stream::WebrtcServiceOptions options;
    options.enabled = true;
    options.local_port_base = 16000;
    live_stream::WebrtcServiceDependencies dependencies;
    dependencies.net_engine = net_engine;
    dependencies.media_service = media_service;
    dependencies.use_fake_engine = false;
    return live_stream::CreateWebrtcService(options, dependencies);
}

std::unique_ptr<live_stream::IHttpService> CreateHttp(
    live_stream::NetEngine* net_engine,
    live_stream::IAuthService* auth_service,
    live_stream::IConfigService* config_service,
    live_stream::ILoggerService* logger_service,
    live_stream::MediaService* media_service,
    live_stream::SnapshotService* snapshot_service,
    live_stream::IWebrtcService* webrtc_service) {
    live_stream::HttpServiceOptions options;
    options.listen_port = 8080;
    options.static_root = kStaticRoot;
    options.enable_static_files = true;
    options.enable_keep_alive = true;
    options.max_requests_per_connection = 32;
    live_stream::HttpServiceDependencies dependencies;
    dependencies.net_engine = net_engine;
    dependencies.auth_service = auth_service;
    dependencies.config_service = config_service;
    dependencies.logger_service = logger_service;
    dependencies.media_service = media_service;
    dependencies.snapshot_service = snapshot_service;
    dependencies.webrtc_service = webrtc_service;
    return live_stream::CreateHttpService(options, dependencies);
}

void StopStartedServices(std::vector<ManagedService>* services) {
    for (auto it = services->rbegin(); it != services->rend(); ++it) {
        if (!it->started) {
            continue;
        }
        INFRA_LOG_INFO("app", "Stop %s", it->name.c_str());
        it->service->Stop();
        it->started = false;
    }
}

void DeinitInitializedServices(std::vector<ManagedService>* services) {
    for (auto it = services->rbegin(); it != services->rend(); ++it) {
        if (!it->initialized) {
            continue;
        }
        INFRA_LOG_INFO("app", "Deinit %s", it->name.c_str());
        it->service->Deinit();
        it->initialized = false;
    }
}

infra::Status InitServices(std::vector<ManagedService>* services) {
    for (ManagedService& entry : *services) {
        INFRA_LOG_INFO("app", "Init %s", entry.name.c_str());
        const infra::Status error = entry.service->Init();
        if (error != infra::Status::kOk) {
            INFRA_LOG_ERROR("app", "Init %s failed: %s", entry.name.c_str(),
                            infra::StatusToString(error));
            return error;
        }
        entry.initialized = true;
    }
    return infra::Status::kOk;
}

infra::Status StartServices(std::vector<ManagedService>* services) {
    for (ManagedService& entry : *services) {
        INFRA_LOG_INFO("app", "Start %s", entry.name.c_str());
        const infra::Status error = entry.service->Start();
        if (error != infra::Status::kOk) {
            INFRA_LOG_ERROR("app", "Start %s failed: %s", entry.name.c_str(),
                            infra::StatusToString(error));
            return error;
        }
        entry.started = true;
    }
    return infra::Status::kOk;
}

void LogPendingServices() {
    for (const char* name : PendingServices) {
        INFRA_LOG_WARN("app", "%s pending: not wired into app registry",
                       name);
    }
}

infra::Status BindMediaConsumers(live_stream::MediaService* media,
                                 live_stream::OsdService* osd,
                                 live_stream::SnapshotService* snapshot) {
    if (media == nullptr) {
        return infra::Status::kInvalidParam;
    }
    infra::Result<live_stream::MediaChannels> channels = media->GetChannels();
    if (!channels.IsOk()) {
        return channels.status;
    }
    if (osd != nullptr) {
        const infra::Status status = osd->BindMedia(channels.value);
        if (status != infra::Status::kOk) {
            return status;
        }
    }
    if (snapshot != nullptr) {
        const infra::Status status = snapshot->BindMedia(channels.value);
        if (status != infra::Status::kOk) {
            return status;
        }
    }
    return infra::Status::kOk;
}

void WaitForStopSignal() {
    while (g_stop_requested == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

}  // namespace

int main() {
    infra::LogConfig log_config;
    log_config.min_level = infra::LogLevel::kInfo;
    log_config.console_output = true;
    log_config.async_write = false;

    if (infra::Log::Init(log_config) != infra::Status::kOk) {
        return 1;
    }

    INFRA_LOG_INFO("app", "live_stream starting");
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    std::vector<ManagedService> services;
    infra::Result<std::unique_ptr<live_stream::NetEngine>> net_engine_result =
        live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!net_engine_result.IsOk()) {
        INFRA_LOG_ERROR("app", "Create net engine failed: %s",
                        infra::StatusToString(net_engine_result.status));
        infra::Log::Shutdown();
        return 1;
    }
    std::unique_ptr<live_stream::NetEngine> net_engine =
        std::move(net_engine_result.value);
    live_stream::NetEngine* net_engine_ptr = net_engine.get();

    std::unique_ptr<live_stream::ILoggerService> logger = CreateLogger();
    live_stream::ILoggerService* logger_ptr = logger.get();
    std::unique_ptr<live_stream::IConfigService> config = CreateConfig();
    live_stream::IConfigService* config_ptr = config.get();
    std::unique_ptr<live_stream::IEventService> event =
        live_stream::CreateEventService();
    live_stream::IEventService* event_ptr = event.get();
    std::unique_ptr<live_stream::IAuthService> auth = CreateAuth(config_ptr);
    live_stream::IAuthService* auth_ptr = auth.get();
    std::unique_ptr<live_stream::MediaService> media = CreateMedia(config_ptr);
    live_stream::MediaService* media_ptr = media.get();
    std::unique_ptr<live_stream::OsdService> osd = CreateOsd(config_ptr);
    live_stream::OsdService* osd_ptr = osd.get();
    std::unique_ptr<live_stream::SnapshotService> snapshot =
        CreateSnapshot(config_ptr);
    live_stream::SnapshotService* snapshot_ptr = snapshot.get();
    std::unique_ptr<live_stream::IRtspService> rtsp =
        CreateRtsp(net_engine_ptr, auth_ptr, event_ptr, media_ptr);
    std::unique_ptr<live_stream::IWebrtcService> webrtc =
        CreateWebrtc(net_engine_ptr, media_ptr);
    live_stream::IWebrtcService* webrtc_ptr = webrtc.get();
    std::unique_ptr<live_stream::IHttpService> http =
        CreateHttp(net_engine_ptr, auth_ptr, config_ptr, logger_ptr, media_ptr,
                   snapshot_ptr, webrtc_ptr);

    services.push_back({"logger_service", std::move(logger)});
    services.push_back({"config_service", std::move(config)});
    services.push_back({"event_service", std::move(event)});
    services.push_back({"auth_service", std::move(auth)});
    services.push_back({"network_service",
                        CreateNetwork(config_ptr, event_ptr, logger_ptr)});
    services.push_back({"media_service", std::move(media)});
    services.push_back({"osd_service", std::move(osd)});
    services.push_back({"snapshot_service", std::move(snapshot)});
    services.push_back({"rtsp_service", std::move(rtsp)});
    services.push_back({"webrtc_service", std::move(webrtc)});
    services.push_back({"http_service", std::move(http)});

    AuthAuditToLoggerSink auth_audit_sink(logger_ptr);
    if (auth_ptr != nullptr) {
        (void)auth_ptr->SetAuditSink(&auth_audit_sink);
    }

    infra::Status error = InitServices(&services);
    if (error == infra::Status::kOk) {
        error = BindMediaConsumers(media_ptr, osd_ptr, snapshot_ptr);
    }
    if (error == infra::Status::kOk) {
        error = StartServices(&services);
    }

    LogPendingServices();
    if (error == infra::Status::kOk) {
        INFRA_LOG_INFO("app", "live_stream running");
        WaitForStopSignal();
    }
    StopStartedServices(&services);
    DeinitInitializedServices(&services);
    if (net_engine) {
        net_engine->Stop();
    }

    INFRA_LOG_INFO("app", "live_stream stopped");
    infra::Log::Shutdown();
    return error == infra::Status::kOk ? 0 : 1;
}
