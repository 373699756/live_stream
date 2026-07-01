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
#include "media/media_source_registry.h"
#include "runtime.h"
#include "service_registry.h"

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

HttpImpl::HttpImpl(const HttpOptions &options,
                   event::Loop *net_loop,
                   INetwork *network,
                   ITime *time,
                   IAlarm *alarm,
                   IUpgrade *upgrade,
                   ISystem *system,
                   IAiReader *ai,
                   DeviceMedia *device,
                   IWebrtc *webrtc)
    : options_(options),
      server_(new HttpServer(
          options,
          HttpServerRefs{Runtime::NetIo(), net_loop},
          this)) {
    InitializeHandlers(network, time, alarm, upgrade, system, ai, device,
                       webrtc);
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

void HttpImpl::InitializeHandlers(INetwork *network,
                                  ITime *time,
                                  IAlarm *alarm,
                                  IUpgrade *upgrade,
                                  ISystem *system,
                                  IAiReader *ai,
                                  DeviceMedia *device,
                                  IWebrtc *webrtc) {
    // handler/router 只在构造期初始化。运行期不重建路由，避免请求线程读 router
    // 时和热重配同时修改 handler 容器。
    router_.Clear();
    handlers_.clear();
    streaming_handler_.reset();
    auth_ = Runtime::Auth();
    logger_ = Runtime::Logger();
    MediaStreams *media_streams = MediaSourceRegistry::Streams();
    const HttpControlRefs control_refs = {
        Runtime::Auth(),
        Runtime::Logger(),
        Runtime::Config(),
        network,
        time,
        alarm,
        upgrade,
        system,
        ServiceRegistry::Rtsp(),
        ServiceRegistry::Onvif(),
        ai,
        device,
        ServiceRegistry::Webrtc(),
        media_streams,
    };
    const HttpMediaRefs media_refs = {
        Runtime::Config(),
        device,
        media_streams,
        ServiceRegistry::Rtsp(),
        ServiceRegistry::Webrtc(),
        webrtc,
    };
    const HttpStreamingRefs streaming_refs = {
        device,
        media_streams,
        Runtime::Event(),
    };
    ConfigureCloseCallback(control_refs.media_streams);
    InitializeControlHandlers(control_refs);
    InitializeMediaHandlers(media_refs);
    InitializeStreamingHandler(streaming_refs);
    RegisterRoutes();
}

void HttpImpl::ConfigureCloseCallback(MediaStreams *media_streams) {
    server_->SetCloseCallback([media_streams](const HttpMediaClientHandle &client) {
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
}

void HttpImpl::InitializeControlHandlers(
    const HttpControlRefs &refs) {
    handlers_.push_back(
        MakeAuthHandler({this, refs.auth}));
    handlers_.push_back(
        MakeConfigHandler({this, refs.config}));
    handlers_.push_back(
        MakeOperationsHandler({this, refs.logger}));
    handlers_.push_back(
        MakeNetworkHandler({this, refs.network}));
    handlers_.push_back(MakeTimeHandler({this, refs.time}));
    handlers_.push_back(MakeUpgradeHandler({this, refs.upgrade}));
    handlers_.push_back(MakeSystemHandler(
        {this,
         refs.system,
         {refs.logger,
          refs.config,
          refs.auth,
          refs.time,
          refs.network,
          refs.alarm,
          refs.upgrade,
          refs.rtsp_session_reader,
          refs.onvif_reader,
          refs.device,
          refs.ai,
          refs.webrtc_reader,
          refs.media_streams}}));
    handlers_.push_back(MakeAlarmHandler({this, refs.alarm}));
    handlers_.push_back(
        MakeAiHandler({this, refs.config, refs.ai, refs.device}));
    handlers_.push_back(
        MakeSnapshotHandler({this, refs.device}));
}

void HttpImpl::InitializeMediaHandlers(
    const HttpMediaRefs &refs) {
    handlers_.push_back(
        MakeMediaHandler({this, refs.config, refs.device,
                          refs.media_streams,
                          refs.rtsp_session_reader,
                          refs.webrtc_reader, this}));
    const HttpMediaHandlerRefs http_media_handler_refs = {
        this, refs.device, refs.media_streams, refs.webrtc};
    const HttpMediaHandlerKind media_handlers[] = {
        HttpMediaHandlerKind::kHls,
        HttpMediaHandlerKind::kWebrtc,
    };
    for (HttpMediaHandlerKind kind : media_handlers) {
        handlers_.push_back(
            CreateHttpHandler(kind, http_media_handler_refs));
    }
}

void HttpImpl::InitializeStreamingHandler(
    const HttpStreamingRefs &refs) {
    streaming_handler_ = CreateStreamingHttpHandler(
        {this, server_.get(), refs.device, refs.media_streams,
         refs.event});
}

void HttpImpl::RegisterRoutes() {
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
           event::Loop *net_loop,
           INetwork *network,
           ITime *time,
           IAlarm *alarm,
           IUpgrade *upgrade,
           ISystem *system,
           IAiReader *ai,
           DeviceMedia *device,
           IWebrtc *webrtc) {
    std::unique_ptr<HttpImpl> service(
        new HttpImpl(options, net_loop, network, time, alarm, upgrade,
                     system, ai, device, webrtc));
    return std::unique_ptr<IHttp>(service.release());
}

}  // namespace live_stream
