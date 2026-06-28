#include "http_impl.h"

#include "http_auth_session.h"
#include "http_media.h"
#include "http_module.h"
#include "http_path.h"
#include "http_protocol.h"
#include "http_request_id.h"
#include "http_response.h"
#include "http_static_files.h"
#include "event.h"
#include "infra/log.h"
#include "infra/time.h"
#include "logger.h"

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
    if (!server_->Prepare()) {
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
    if (!server_->Start()) {
        Error(kHttpModuleName, "HTTP server start failed");
        return false;
    }
    return true;
}

void HttpImpl::Stop() {
    StopInternal();
}

void HttpImpl::StopInternal() {
    server_->Stop();
}

void HttpImpl::Release() {
    ReleaseInternal();
}

void HttpImpl::ReleaseInternal() {
    StopInternal();
    std::lock_guard<std::mutex> guard(mutex_);
    server_->Release();
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
    // 这些入口可能创建长连接、拉取大块媒体或等待 WebRTC signaling，
    // 必须走 stream executor，避免阻塞登录、配置、状态查询等控制面。
    if (request.method == HttpMethod::kGet) {
        return StartsWith(request.path, "/live/") ||
               StartsWith(request.path, "/snapshot/") ||
               request.path == "/api/events";
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
                StatusResponse(500, "Bus not initialized"));
        }
    }
    server_->IncrementTotalRequests();

    if (request_with_id.path.empty() || request_with_id.path[0] != '/') {
        IncrementParseFailures();
        return AddJsonEnvelope(request_with_id,
                               StatusResponse(400, "Invalid request path"));
    }

    const HttpRouteMatch route = router_.Match(request_with_id);
    if (route.found && route.callback) {
        return AddJsonEnvelope(
            request_with_id,
            route.callback(request_with_id));
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

HttpStreamingRequestResult HttpImpl::HandleStreamingHttpRequest(
    ConnectionId connection_id, const HttpRequest &request) {
    IStreamingHttpHandler *streaming_handler = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        streaming_handler = streaming_handler_.get();
    }
    if (streaming_handler == nullptr ||
        !streaming_handler->CanHandleStreamingRequest(request)) {
        return HttpStreamingRequestResult::kNotHandled;
    }
    server_->IncrementTotalRequests();
    return streaming_handler->HandleStreamingRequest(connection_id, request);
}

HttpStats HttpImpl::GetStats() const {
    return server_->GetStats();
}

std::vector<HttpStreamSessionInfo>
HttpImpl::ListStreamSessionInfo() const {
    return server_->ListStreamSessionInfo();
}

HttpListenAddress HttpImpl::LocalAddress() const {
    return server_->LocalAddress();
}

void HttpImpl::InitializeHandlers(const HttpDependencies &dependencies) {
    // handler/router 只在构造期初始化。运行期不重建路由，避免请求线程读 router
    // 时和热重配同时修改 handler 容器。
    router_.Clear();
    handlers_.clear();
    streaming_handler_.reset();
    auth_ = dependencies.auth;
    logger_ = dependencies.logger;
    server_->SetCloseCallback([media_streams =
                                   dependencies.media_streams](
                                  const HttpMediaClientHandle &client) {
        if (client.type == HttpMediaClientType::kFlv &&
            media_streams != nullptr && client.id != 0) {
            (void)media_streams->DetachFlvClient(client.id);
        }
        if (client.type == HttpMediaClientType::kMjpeg &&
            media_streams != nullptr && client.id != 0) {
            (void)media_streams->DetachMjpegClient(client.id);
        }
        if (client.type == HttpMediaClientType::kEventStream &&
            client.event_subscription != nullptr) {
            client.event_subscription->Cancel();
        }
    });

    const AuthHandlerDependencies auth_handler_dependencies = {
        this, dependencies.auth};
    const ConfigHandlerDependencies config_handler_dependencies = {
        this, dependencies.config};
    const OperationsHandlerDependencies operations_handler_dependencies = {
        this, dependencies.logger};
    const NetworkHandlerDependencies network_handler_dependencies = {
        this, dependencies.network};
    const TimeHandlerDependencies time_handler_dependencies = {
        this, dependencies.time};
    const UpgradeHandlerDependencies upgrade_handler_dependencies = {
        this, dependencies.upgrade};
    const SystemHandlerDependencies system_handler_dependencies = {
        this,
        dependencies.system,
        {
            dependencies.logger,
            dependencies.config,
            dependencies.auth,
            dependencies.time,
            dependencies.network,
            dependencies.alarm,
            dependencies.upgrade,
            dependencies.rtsp_session_reader,
            dependencies.onvif_status_reader,
            dependencies.device,
            dependencies.ai,
            dependencies.webrtc_status_reader,
            dependencies.media_streams,
        },
    };
    const AlarmHandlerDependencies alarm_handler_dependencies = {
        this, dependencies.alarm};
    const MediaHandlerDependencies media_handler_dependencies = {
        this,
        dependencies.config,
        dependencies.device,
        dependencies.media_streams,
        dependencies.rtsp_session_reader,
        dependencies.webrtc_status_reader,
        this};
    const AiHandlerDependencies ai_handler_dependencies = {
        this, dependencies.config, dependencies.ai, dependencies.device};
    const SnapshotHandlerDependencies snapshot_handler_dependencies = {
        this, dependencies.device};

    handlers_.push_back(MakeAuthHandler(auth_handler_dependencies));
    handlers_.push_back(MakeConfigHandler(config_handler_dependencies));
    handlers_.push_back(MakeOperationsHandler(operations_handler_dependencies));
    handlers_.push_back(MakeNetworkHandler(network_handler_dependencies));
    handlers_.push_back(MakeTimeHandler(time_handler_dependencies));
    handlers_.push_back(MakeUpgradeHandler(upgrade_handler_dependencies));
    handlers_.push_back(MakeSystemHandler(system_handler_dependencies));
    handlers_.push_back(MakeAlarmHandler(alarm_handler_dependencies));
    handlers_.push_back(MakeMediaHandler(media_handler_dependencies));
    handlers_.push_back(MakeAiHandler(ai_handler_dependencies));
    handlers_.push_back(MakeSnapshotHandler(snapshot_handler_dependencies));
    HttpMediaHandlerDependencies http_media_handler_dependencies;
    http_media_handler_dependencies.access = this;
    http_media_handler_dependencies.device = dependencies.device;
    http_media_handler_dependencies.media_streams = dependencies.media_streams;
    http_media_handler_dependencies.webrtc = dependencies.webrtc;
    const HttpMediaHandlerKind media_handlers[] = {
        HttpMediaHandlerKind::kHls,
        HttpMediaHandlerKind::kWebrtc,
    };
    for (HttpMediaHandlerKind kind : media_handlers) {
        handlers_.push_back(
            CreateHttpHandler(kind, http_media_handler_dependencies));
    }
    StreamingHttpHandlerDependencies streaming_handler_dependencies;
    streaming_handler_dependencies.access = this;
    streaming_handler_dependencies.writer = server_.get();
    streaming_handler_dependencies.device = dependencies.device;
    streaming_handler_dependencies.media_streams = dependencies.media_streams;
    streaming_handler_dependencies.event = dependencies.event;
    streaming_handler_ = CreateStreamingHttpHandler(
        streaming_handler_dependencies);

    for (const std::unique_ptr<IHttpHandler> &handler : handlers_) {
        if (handler != nullptr) {
            handler->RegisterRoutes(router_);
        }
    }
}

void HttpImpl::IncrementParseFailures() {
    server_->IncrementParseFailures();
}

void HttpImpl::IncrementNotFound() {
    server_->IncrementNotFound();
}

void HttpImpl::IncrementAuthFailures() {
    server_->IncrementAuthFailures();
}

void HttpImpl::IncrementPermissionDenied() {
    server_->IncrementPermissionDenied();
}

live_stream::RequestContext HttpImpl::MakeContext(
    const HttpRequest &request, const AuthPrincipal *principal) {
    live_stream::RequestContext context;
    context.request_id =
        request.request_id.empty() ? MakeRequestId(NextRequestId())
                                   : request.request_id;
    context.client_ip = request.client_ip;
    context.user_agent = GetHeader(request, "User-Agent");
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
    const std::string token = ExtractAuthToken(request);
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
