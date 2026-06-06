#include "http_service_impl.h"

#include "http_console_service.h"
#include "http_handler_utils.h"
#include "http_request_utils.h"
#include "http_static_files.h"
#include "infra/log.h"
#include "infra/time.h"
#include "logger_service.h"
#include "media_source.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace live_stream {
namespace {

const char *StaticStatusText(StaticFileStatus status) {
    switch (status) {
        case StaticFileStatus::kOk:
            return "ok";
        case StaticFileStatus::kNotFound:
            return "not_found";
        case StaticFileStatus::kForbidden:
            return "forbidden";
    }
    return "unknown";
}

}  // namespace

HttpServiceImpl::HttpServiceImpl(
    const HttpServiceOptions &options,
    const HttpServiceDependencies &dependencies)
    : options_(options),
      dependencies_(dependencies),
      server_(new HttpServer(options, dependencies, this)) {}

HttpServiceImpl::~HttpServiceImpl() {
    ReleaseInternal();
}

bool HttpServiceImpl::Prepare() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (initialized_) {
        return true;
    }
    if (server_ == nullptr || !server_->Prepare()) {
        return false;
    }
    initialized_ = true;
    return true;
}

bool HttpServiceImpl::Start() {
    if (!Prepare()) {
        INFRA_LOG_ERROR(kHttpModuleName, "HTTP start prepare failed");
        return false;
    }
    if (server_ == nullptr || !server_->Start()) {
        INFRA_LOG_ERROR(kHttpModuleName, "HTTP server start failed");
        return false;
    }
    return true;
}

void HttpServiceImpl::Stop() {
    StopInternal();
}

void HttpServiceImpl::StopInternal() {
    if (server_ != nullptr) {
        server_->Stop();
    }
}

void HttpServiceImpl::Release() {
    ReleaseInternal();
}

void HttpServiceImpl::ReleaseInternal() {
    StopInternal();
    std::lock_guard<std::mutex> guard(mutex_);
    if (server_ != nullptr) {
        server_->Release();
    }
    streaming_handler_.reset();
    handlers_.clear();
    router_.Clear();
    initialized_ = false;
}

HttpResponse HttpServiceImpl::HandleRequest(const HttpRequest &request) {
    return HandleHttpRequest(request);
}

bool HttpServiceImpl::ShouldUseStreamExecutor(
    const HttpRequest &request) const {
    if (request.method == HttpMethod::kGet) {
        return StartsWith(request.path, "/api/flv/") ||
               StartsWith(request.path, "/api/mjpeg/") ||
               StartsWith(request.path, "/api/hls/") ||
               StartsWith(request.path, "/api/snapshot/");
    }
    return StartsWith(request.path, "/api/webrtc") &&
           (request.method == HttpMethod::kPost ||
            request.method == HttpMethod::kDelete);
}

HttpResponse HttpServiceImpl::HandleHttpRequest(const HttpRequest &request) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!initialized_) {
            return StatusResponse(500, "Service not initialized");
        }
    }
    if (server_ != nullptr) {
        server_->IncrementTotalRequests();
    }

    if (request.path.empty() || request.path[0] != '/') {
        IncrementParseFailures();
        return StatusResponse(400, "Invalid request path");
    }

    const HttpRouteMatch route = router_.Match(request);
    if (route.found && route.callback != nullptr) {
        return route.callback(route.user, request);
    }
    if (StartsWith(request.path, "/api/")) {
        return StatusResponse(501, "Not Implemented");
    }
    if (request.method == HttpMethod::kGet && options_.enable_static_files) {
        return HandleStaticFile(request);
    }

    IncrementNotFound();
    return StatusResponse(404, "Not Found");
}

bool HttpServiceImpl::HandleStreamingHttpRequest(
    ConnectionId connection_id, const HttpRequest &request) {
    IStreamingHttpHandler *streaming_handler = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        streaming_handler = streaming_handler_.get();
    }
    if (streaming_handler == nullptr ||
        !streaming_handler->CanHandleStreamingRequest(request)) {
        return false;
    }
    if (server_ != nullptr) {
        server_->IncrementTotalRequests();
    }
    INFRA_LOG_INFO(kHttpModuleName, "HTTP stream request conn=%llu path=%s peer=%s",
                   static_cast<unsigned long long>(connection_id),
                   request.path.c_str(), request.client_ip.c_str());
    streaming_handler->HandleStreamingRequest(connection_id, request);
    return true;
}

HttpServiceStats HttpServiceImpl::GetStats() const {
    if (server_ == nullptr) {
        return HttpServiceStats{};
    }
    return server_->GetStats();
}

HttpListenAddress HttpServiceImpl::LocalAddress() const {
    if (server_ == nullptr) {
        return HttpListenAddress{};
    }
    return server_->LocalAddress();
}

void HttpServiceImpl::ConfigureConsoleHandlers(
    IAuthService *auth_service, ILoggerService *logger_service,
    IConfigService *config_service, INetworkService *network_service,
    ITimeService *time_service, IAlarmService *alarm_service,
    IUpgradeService *upgrade_service, ISystemService *system_service,
    IRtspService *rtsp_service, IOnvifService *onvif_service,
    IAiView *ai_service, IMediaService *media_service,
    ISnapshotView *snapshot_service, IWebrtcService *webrtc_service,
    IMediaSource *media_source,
    IMediaFlvSource *media_flv_source,
    IMediaMjpegSource *media_mjpeg_source) {
    router_.Clear();
    handlers_.clear();
    streaming_handler_.reset();
    auth_service_ = auth_service;
    logger_service_ = logger_service;
    if (server_ != nullptr) {
        server_->SetCloseCallback([media_flv_source,
                                   media_mjpeg_source](HttpStreamClientId id) {
            if (media_flv_source != nullptr && id != 0) {
                (void)media_flv_source->DetachFlvClient(id);
            }
            if (media_mjpeg_source != nullptr && id != 0) {
                (void)media_mjpeg_source->DetachMjpegClient(id);
            }
        });
    }

    handlers_.push_back(CreateAuthHttpHandler(this, auth_service));
    handlers_.push_back(CreateConfigHttpHandler(this, config_service));
    handlers_.push_back(CreateOperationsHttpHandler(this, logger_service));
    handlers_.push_back(CreateNetworkHttpHandler(this, network_service));
    handlers_.push_back(CreateTimeHttpHandler(this, time_service));
    handlers_.push_back(CreateUpgradeHttpHandler(this, upgrade_service));

    SystemStatusSources system_status_sources;
    system_status_sources.logger_service = logger_service;
    system_status_sources.config_service = config_service;
    system_status_sources.auth_service = auth_service;
    system_status_sources.time_service = time_service;
    system_status_sources.network_service = network_service;
    system_status_sources.alarm_service = alarm_service;
    system_status_sources.upgrade_service = upgrade_service;
    system_status_sources.rtsp_service = rtsp_service;
    system_status_sources.onvif_service = onvif_service;
    system_status_sources.media_service = media_service;
    system_status_sources.ai_service = ai_service;
    system_status_sources.snapshot_service = snapshot_service;
    system_status_sources.webrtc_service = webrtc_service;
    system_status_sources.media_source = media_source;
    handlers_.push_back(CreateSystemHttpHandler(
        this, system_service, system_status_sources));

    handlers_.push_back(CreateMediaHttpHandler(
        this, config_service, media_service, media_source,
        webrtc_service));
    handlers_.push_back(CreateAiHttpHandler(this, config_service, ai_service));
    handlers_.push_back(CreateSnapshotHttpHandler(
        this, media_service, snapshot_service));
    handlers_.push_back(CreateHlsHttpHandler(
        this, media_service, media_source));
    handlers_.push_back(CreateWebrtcHttpHandler(
        this, media_service, webrtc_service));
    handlers_.push_back(CreateEventStreamHttpHandler(this));
    streaming_handler_ = CreateStreamingHttpHandler(
        this, server_.get(), media_service, media_source,
        media_flv_source, media_mjpeg_source);

    for (const std::unique_ptr<IHttpHandler> &handler : handlers_) {
        if (handler != nullptr) {
            handler->RegisterRoutes(&router_);
        }
    }
}

void HttpServiceImpl::IncrementParseFailures() {
    if (server_ != nullptr) {
        server_->IncrementParseFailures();
    }
}

void HttpServiceImpl::IncrementNotFound() {
    if (server_ != nullptr) {
        server_->IncrementNotFound();
    }
}

void HttpServiceImpl::IncrementAuthFailures() {
    if (server_ != nullptr) {
        server_->IncrementAuthFailures();
    }
}

void HttpServiceImpl::IncrementPermissionDenied() {
    if (server_ != nullptr) {
        server_->IncrementPermissionDenied();
    }
}

live_stream::RequestContext HttpServiceImpl::MakeContext(
    const HttpRequest &request, const AuthPrincipal *principal) {
    live_stream::RequestContext context;
    context.request_id = MakeRequestId(NextRequestId());
    context.client_ip = request.client_ip;
    context.user_agent = RequestUserAgent(request);
    if (principal != nullptr) {
        context.user_name = principal->user_name;
        context.session_id = principal->session_id;
    }
    return context;
}

uint64_t HttpServiceImpl::NextRequestId() {
    std::lock_guard<std::mutex> guard(mutex_);
    return ++next_request_id_;
}

AuthPrincipal HttpServiceImpl::Authenticate(const HttpRequest &request) {
    const std::string token = ExtractBearerToken(request);
    if (token.empty()) {
        IncrementAuthFailures();
        return AuthPrincipal{};
    }
    if (auth_service_ == nullptr) {
        IncrementAuthFailures();
        return AuthPrincipal{};
    }
    TokenValidationResult validated = auth_service_->ValidateToken(token);
    if (validated.principal.user_name.empty()) {
        IncrementAuthFailures();
        return AuthPrincipal{};
    }
    return validated.principal;
}

bool HttpServiceImpl::RequirePermission(const HttpRequest &request,
                                        AuthPermission permission,
                                        const std::string &target,
                                        AuthPrincipal *principal) {
    AuthPrincipal authenticated = Authenticate(request);
    if (authenticated.user_name.empty()) {
        return false;
    }
    if (principal != nullptr) {
        *principal = authenticated;
    }
    if (authenticated.must_change_password) {
        IncrementPermissionDenied();
        RecordOperation(request, authenticated,
                        OperationAction::kPermissionDenied, target,
                        OperationResult::kRejected,
                        "must_change_password");
        return false;
    }
    if (auth_service_ == nullptr ||
        !auth_service_->CheckPermission(authenticated, permission, target)) {
        IncrementPermissionDenied();
        RecordOperation(request, authenticated,
                        OperationAction::kPermissionDenied, target,
                        OperationResult::kRejected, "permission_denied");
        return false;
    }
    return true;
}

void HttpServiceImpl::RecordOperation(
    const HttpRequest &request, const AuthPrincipal &principal,
    OperationAction action, const std::string &target, OperationResult result,
    const std::string &reason) {
    if (logger_service_ == nullptr) {
        return;
    }
    live_stream::RequestContext context = MakeContext(request, &principal);
    OperationRecord record;
    record.timestamp_ms = infra::Time::SystemTimeMillis();
    record.request_id = context.request_id;
    record.user_name = context.user_name;
    record.session_id = context.session_id;
    record.client_ip = context.client_ip;
    record.module = kHttpModuleName;
    record.action = action;
    record.target = target;
    record.result = result;
    record.reason = reason;
    (void)logger_service_->RecordOperation(record);
}

HttpResponse HttpServiceImpl::HandleStaticFile(const HttpRequest &request) {
    const StaticFileResult result =
        BuildStaticFileResponse(request, options_.static_root);
    if (result.status == StaticFileStatus::kNotFound) {
        IncrementNotFound();
        return StatusResponse(404, "Not Found");
    }
    if (result.status == StaticFileStatus::kForbidden) {
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HTTP static reject status=%s request=%s relative=%s "
                        "path=%s",
                        StaticStatusText(result.status), request.path.c_str(),
                        result.relative_path.c_str(), result.path.c_str());
        return StatusResponse(403, "Forbidden");
    }
    return result.response;
}

std::unique_ptr<IHttpService>
CreateHttpService(const HttpServiceOptions &options,
                  const HttpServiceDependencies &dependencies) {
    return std::unique_ptr<IHttpService>(
        new HttpServiceImpl(options, dependencies));
}

std::unique_ptr<IHttpService> CreateHttpConsoleService(
    const HttpServiceOptions &options, NetEngine *net_engine,
    IAuthService *auth_service, ILoggerService *logger_service,
    IConfigService *config_service, INetworkService *network_service,
    ITimeService *time_service, IAlarmService *alarm_service,
    IUpgradeService *upgrade_service, ISystemService *system_service,
    IRtspService *rtsp_service, IOnvifService *onvif_service,
    IAiView *ai_service, IMediaService *media_service,
    ISnapshotView *snapshot_service, IWebrtcService *webrtc_service,
    IMediaSource *media_source,
    IMediaFlvSource *media_flv_source,
    IMediaMjpegSource *media_mjpeg_source) {
    HttpServiceDependencies dependencies;
    dependencies.net_engine = net_engine;
    std::unique_ptr<HttpServiceImpl> service(
        new HttpServiceImpl(options, dependencies));
    service->ConfigureConsoleHandlers(
        auth_service, logger_service, config_service, network_service,
        time_service, alarm_service, upgrade_service, system_service,
        rtsp_service, onvif_service, ai_service, media_service,
        snapshot_service, webrtc_service, media_source,
        media_flv_source, media_mjpeg_source);
    return std::unique_ptr<IHttpService>(service.release());
}

}  // namespace live_stream
