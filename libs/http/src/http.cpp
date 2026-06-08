#include "http_impl.h"

#include "http_handler_utils.h"
#include "http_media.h"
#include "http_request_utils.h"
#include "http_static_files.h"
#include "infra/log.h"
#include "infra/time.h"
#include "logger.h"
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

HttpImpl::HttpImpl(
    const HttpOptions &options,
    const HttpDependencies &dependencies)
    : options_(options),
      server_(new HttpServer(options, dependencies, this)) {
    InitializeHandlers(dependencies);
}

HttpImpl::~HttpImpl() {
    ReleaseInternal();
}

bool HttpImpl::Prepare() {
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

bool HttpImpl::Start() {
    if (!Prepare()) {
        Error(kHttpModuleName, "HTTP start prepare failed");
        return false;
    }
    if (server_ == nullptr || !server_->Start()) {
        Error(kHttpModuleName, "HTTP server start failed");
        return false;
    }
    return true;
}

void HttpImpl::Stop() {
    StopInternal();
}

void HttpImpl::StopInternal() {
    if (server_ != nullptr) {
        server_->Stop();
    }
}

void HttpImpl::Release() {
    ReleaseInternal();
}

void HttpImpl::ReleaseInternal() {
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

HttpResponse HttpImpl::HandleRequest(const HttpRequest &request) {
    return HandleHttpRequest(request);
}

bool HttpImpl::ShouldUseStreamExecutor(
    const HttpRequest &request) const {
    if (request.method == HttpMethod::kGet) {
        return StartsWith(request.path, "/live/") ||
               StartsWith(request.path, "/snapshot/");
    }
    if (StartsWith(request.path, "/live/") &&
        (request.method == HttpMethod::kPost ||
         request.method == HttpMethod::kDelete)) {
        return true;
    }
    return StartsWith(request.path, "/api/webrtc") &&
           (request.method == HttpMethod::kPost ||
            request.method == HttpMethod::kDelete);
}

HttpResponse HttpImpl::HandleHttpRequest(const HttpRequest &request) {
    HttpRequest request_with_id = request;
    if (request_with_id.request_id.empty()) {
        request_with_id.request_id = MakeRequestId(NextRequestId());
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!initialized_) {
            return AddJsonEnvelope(
                request_with_id,
                StatusResponse(500, "Service not initialized"));
        }
    }
    if (server_ != nullptr) {
        server_->IncrementTotalRequests();
    }

    if (request_with_id.path.empty() || request_with_id.path[0] != '/') {
        IncrementParseFailures();
        return AddJsonEnvelope(request_with_id,
                               StatusResponse(400, "Invalid request path"));
    }

    const HttpRouteMatch route = router_.Match(request_with_id);
    if (route.found && route.callback != nullptr) {
        return AddJsonEnvelope(
            request_with_id,
            route.callback(route.user, request_with_id));
    }
    if (StartsWith(request_with_id.path, "/api/")) {
        return AddJsonEnvelope(request_with_id,
                               StatusResponse(501, "Not Implemented"));
    }
    if (request_with_id.method == HttpMethod::kGet &&
        options_.enable_static_files) {
        return HandleStaticFile(request_with_id);
    }

    IncrementNotFound();
    return AddJsonEnvelope(request_with_id,
                           StatusResponse(404, "Not Found"));
}

bool HttpImpl::HandleStreamingHttpRequest(
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
    Info(kHttpModuleName, "HTTP stream request conn=%llu path=%s peer=%s",
                   static_cast<unsigned long long>(connection_id),
                   request.path.c_str(), request.client_ip.c_str());
    streaming_handler->HandleStreamingRequest(connection_id, request);
    return true;
}

HttpStats HttpImpl::GetStats() const {
    if (server_ == nullptr) {
        return HttpStats{};
    }
    return server_->GetStats();
}

HttpListenAddress HttpImpl::LocalAddress() const {
    if (server_ == nullptr) {
        return HttpListenAddress{};
    }
    return server_->LocalAddress();
}

void HttpImpl::InitializeHandlers(const HttpDependencies &dependencies) {
    router_.Clear();
    handlers_.clear();
    streaming_handler_.reset();
    auth_ = dependencies.auth;
    logger_ = dependencies.logger;
    if (server_ != nullptr) {
        server_->SetCloseCallback([media_flv_source =
                                       dependencies.media_flv_source,
                                   media_mjpeg_source =
                                       dependencies.media_mjpeg_source](
                                      const HttpMediaClientHandle &client) {
            if (client.type == HttpMediaClientType::kFlv &&
                media_flv_source != nullptr && client.id != 0) {
                (void)media_flv_source->DetachFlvClient(client.id);
            }
            if (client.type == HttpMediaClientType::kMjpeg &&
                media_mjpeg_source != nullptr && client.id != 0) {
                (void)media_mjpeg_source->DetachMjpegClient(client.id);
            }
        });
    }

    HttpHandlerDependencies handler_dependencies;
    handler_dependencies.access = this;
    handler_dependencies.auth = dependencies.auth;
    handler_dependencies.config = dependencies.config;
    handler_dependencies.logger = dependencies.logger;
    handler_dependencies.network_config = dependencies.network_config;
    handler_dependencies.time = dependencies.time;
    handler_dependencies.upgrade = dependencies.upgrade;
    handler_dependencies.system = dependencies.system;
    handler_dependencies.device_media = dependencies.device_media;
    handler_dependencies.media_source = dependencies.media_source;
    handler_dependencies.rtsp = dependencies.rtsp;
    handler_dependencies.webrtc = dependencies.webrtc;
    handler_dependencies.ai = dependencies.ai;
    handler_dependencies.snapshot = dependencies.snapshot;

    handlers_.push_back(CreateHttpHandler(
        HttpHandlerKind::kAuth, handler_dependencies));
    handlers_.push_back(CreateHttpHandler(
        HttpHandlerKind::kConfig, handler_dependencies));
    handlers_.push_back(CreateHttpHandler(
        HttpHandlerKind::kOperations, handler_dependencies));
    handlers_.push_back(CreateHttpHandler(
        HttpHandlerKind::kNetwork, handler_dependencies));
    handlers_.push_back(CreateHttpHandler(
        HttpHandlerKind::kTime, handler_dependencies));
    handlers_.push_back(CreateHttpHandler(
        HttpHandlerKind::kUpgrade, handler_dependencies));

    SystemStatusSources system_status_sources;
    system_status_sources.logger = dependencies.logger;
    system_status_sources.config = dependencies.config;
    system_status_sources.auth = dependencies.auth;
    system_status_sources.time = dependencies.time;
    system_status_sources.network_config = dependencies.network_config;
    system_status_sources.alarm = dependencies.alarm;
    system_status_sources.upgrade = dependencies.upgrade;
    system_status_sources.rtsp = dependencies.rtsp;
    system_status_sources.onvif = dependencies.onvif;
    system_status_sources.device_media = dependencies.device_media;
    system_status_sources.ai = dependencies.ai;
    system_status_sources.snapshot = dependencies.snapshot;
    system_status_sources.webrtc = dependencies.webrtc;
    system_status_sources.media_source = dependencies.media_source;
    handler_dependencies.system_status_sources = system_status_sources;
    handlers_.push_back(CreateHttpHandler(
        HttpHandlerKind::kSystem, handler_dependencies));

    handlers_.push_back(CreateHttpHandler(
        HttpHandlerKind::kMedia, handler_dependencies));
    handlers_.push_back(CreateHttpHandler(
        HttpHandlerKind::kAi, handler_dependencies));
    handlers_.push_back(CreateHttpHandler(
        HttpHandlerKind::kSnapshot, handler_dependencies));
    HttpMediaHandlerDependencies media_handler_dependencies;
    media_handler_dependencies.access = this;
    media_handler_dependencies.device_media = dependencies.device_media;
    media_handler_dependencies.media_source = dependencies.media_source;
    media_handler_dependencies.webrtc = dependencies.webrtc;
    handlers_.push_back(CreateHttpHandler(
        HttpMediaHandlerKind::kHls, media_handler_dependencies));
    handlers_.push_back(CreateHttpHandler(
        HttpMediaHandlerKind::kWebrtc, media_handler_dependencies));
    handlers_.push_back(CreateHttpHandler(
        HttpHandlerKind::kEventStream, handler_dependencies));
    StreamingHttpHandlerDependencies streaming_handler_dependencies;
    streaming_handler_dependencies.access = this;
    streaming_handler_dependencies.writer = server_.get();
    streaming_handler_dependencies.device_media = dependencies.device_media;
    streaming_handler_dependencies.media_source = dependencies.media_source;
    streaming_handler_dependencies.media_flv_source =
        dependencies.media_flv_source;
    streaming_handler_dependencies.media_mjpeg_source =
        dependencies.media_mjpeg_source;
    streaming_handler_ = CreateStreamingHttpHandler(
        streaming_handler_dependencies);

    for (const std::unique_ptr<IHttpHandler> &handler : handlers_) {
        if (handler != nullptr) {
            handler->RegisterRoutes(&router_);
        }
    }
}

void HttpImpl::IncrementParseFailures() {
    if (server_ != nullptr) {
        server_->IncrementParseFailures();
    }
}

void HttpImpl::IncrementNotFound() {
    if (server_ != nullptr) {
        server_->IncrementNotFound();
    }
}

void HttpImpl::IncrementAuthFailures() {
    if (server_ != nullptr) {
        server_->IncrementAuthFailures();
    }
}

void HttpImpl::IncrementPermissionDenied() {
    if (server_ != nullptr) {
        server_->IncrementPermissionDenied();
    }
}

live_stream::RequestContext HttpImpl::MakeContext(
    const HttpRequest &request, const AuthPrincipal *principal) {
    live_stream::RequestContext context;
    context.request_id =
        request.request_id.empty() ? MakeRequestId(NextRequestId())
                                   : request.request_id;
    context.client_ip = request.client_ip;
    context.user_agent = RequestUserAgent(request);
    if (principal != nullptr) {
        context.user_name = principal->user_name;
        context.session_id = principal->session_id;
    }
    return context;
}

uint64_t HttpImpl::NextRequestId() {
    std::lock_guard<std::mutex> guard(mutex_);
    return ++next_request_id_;
}

AuthPrincipal HttpImpl::Authenticate(const HttpRequest &request) {
    const std::string token = ExtractBearerToken(request);
    if (token.empty()) {
        IncrementAuthFailures();
        return AuthPrincipal{};
    }
    if (auth_ == nullptr) {
        IncrementAuthFailures();
        return AuthPrincipal{};
    }
    TokenValidationResult validated = auth_->ValidateToken(token);
    if (validated.principal.user_name.empty()) {
        IncrementAuthFailures();
        return AuthPrincipal{};
    }
    return validated.principal;
}

bool HttpImpl::RequirePermission(const HttpRequest &request,
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
    if (auth_ == nullptr ||
        !auth_->CheckPermission(authenticated, permission, target)) {
        IncrementPermissionDenied();
        RecordOperation(request, authenticated,
                        OperationAction::kPermissionDenied, target,
                        OperationResult::kRejected, "permission_denied");
        return false;
    }
    return true;
}

void HttpImpl::RecordOperation(
    const HttpRequest &request, const AuthPrincipal &principal,
    OperationAction action, const std::string &target, OperationResult result,
    const std::string &reason) {
    if (logger_ == nullptr) {
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
    (void)logger_->RecordOperation(record);
}

HttpResponse HttpImpl::HandleStaticFile(const HttpRequest &request) {
    const StaticFileResult result =
        BuildStaticFileResponse(request, options_.static_root);
    if (result.status == StaticFileStatus::kNotFound) {
        IncrementNotFound();
        return StatusResponse(404, "Not Found");
    }
    if (result.status == StaticFileStatus::kForbidden) {
        Error(kHttpModuleName,
                        "HTTP static reject status=%s request=%s relative=%s "
                        "path=%s",
                        StaticStatusText(result.status), request.path.c_str(),
                        result.relative_path.c_str(), result.path.c_str());
        return StatusResponse(403, "Forbidden");
    }
    return result.response;
}

std::unique_ptr<IHttp>
CreateHttp(const HttpOptions &options,
                  const HttpDependencies &dependencies) {
    std::unique_ptr<HttpImpl> service(
        new HttpImpl(options, dependencies));
    return std::unique_ptr<IHttp>(service.release());
}

}  // namespace live_stream
