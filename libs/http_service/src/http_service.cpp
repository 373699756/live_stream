#include "http_service_impl.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "handlers/http_handlers.h"
#include "http_handler_utils.h"
#include "http_protocol.h"
#include "http_request_utils.h"
#include "http_connection_store.h"
#include "http_router.h"
#include "http_static_files.h"
#include "infra/executor.h"
#include "infra/log.h"
#include "infra/time.h"
#include "logger_service.h"
#include "net_service.h"
#include "stream_hub_service.h"

namespace live_stream {
namespace {

constexpr size_t kMaxStreamingQueuedBytes = 1024U * 1024U;

const char *HttpMethodName(HttpMethod method) {
    switch (method) {
        case HttpMethod::kGet:
            return "GET";
        case HttpMethod::kPost:
            return "POST";
        case HttpMethod::kPut:
            return "PUT";
        case HttpMethod::kDelete:
            return "DELETE";
    }
    return "UNKNOWN";
}

bool StartExecutor(infra::Executor *executor, uint32_t worker_count,
                   uint32_t queue_capacity) {
    if (executor == nullptr || worker_count == 0 || queue_capacity == 0) {
        return false;
    }
    infra::ExecutorOptions options;
    options.worker_count = worker_count;
    options.queue_capacity = queue_capacity;
    return executor->Start(options);
}

void StopExecutor(infra::Executor *executor) {
    if (executor != nullptr) {
        executor->Stop(infra::StopMode::kDiscard);
    }
}

}  // namespace

HttpServiceImpl::HttpServiceImpl(const HttpServiceOptions &options,
                                 const HttpServiceDependencies &dependencies)
    : options_(options), dependencies_(dependencies) {}

HttpServiceImpl::~HttpServiceImpl() {
    Stop();
    Release();
}

bool HttpServiceImpl::Prepare() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (initialized_) {
        return true;
    }
    if (dependencies_.auth_service == nullptr ||
        dependencies_.config_service == nullptr) {
        return false;
    }
    if (options_.max_request_header_bytes == 0 ||
        options_.max_request_body_bytes == 0 || options_.max_connections == 0 ||
        options_.request_timeout_ms == 0 ||
        options_.connection_idle_timeout_ms == 0 ||
        options_.max_requests_per_connection == 0 ||
        options_.max_pipelined_requests == 0 ||
        options_.executor_worker_count == 0 ||
        options_.executor_queue_capacity == 0 ||
        options_.stream_executor_worker_count == 0 ||
        options_.stream_executor_queue_capacity == 0 ||
        options_.control_executor_worker_count == 0 ||
        options_.control_executor_queue_capacity == 0 ||
        options_.config_apply_worker_count == 0 ||
        options_.config_apply_queue_capacity == 0) {
        return false;
    }
    task_executor_.reset(new infra::Executor());
    stream_executor_.reset(new infra::Executor());
    control_executor_.reset(new infra::Executor());
    config_apply_executor_.reset(new infra::Executor());
    initialized_ = true;
    return true;
}

bool HttpServiceImpl::Start() {
    if (!Prepare()) {
        return false;
    }
    infra::Executor *task_executor = nullptr;
    infra::Executor *stream_executor = nullptr;
    infra::Executor *control_executor = nullptr;
    infra::Executor *config_apply_executor = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (started_) {
            return true;
        }
        if (dependencies_.net_engine == nullptr) {
            return false;
        }
        task_executor = task_executor_.get();
        stream_executor = stream_executor_.get();
        control_executor = control_executor_.get();
        config_apply_executor = config_apply_executor_.get();
    }
    if (!StartExecutor(task_executor, options_.executor_worker_count,
                       options_.executor_queue_capacity)) {
        return false;
    }
    if (!StartExecutor(stream_executor, options_.stream_executor_worker_count,
                       options_.stream_executor_queue_capacity)) {
        StopExecutor(task_executor);
        return false;
    }
    if (!StartExecutor(control_executor, options_.control_executor_worker_count,
                       options_.control_executor_queue_capacity)) {
        StopExecutor(stream_executor);
        StopExecutor(task_executor);
        return false;
    }
    if (!StartExecutor(config_apply_executor,
                       options_.config_apply_worker_count,
                       options_.config_apply_queue_capacity)) {
        StopExecutor(control_executor);
        StopExecutor(stream_executor);
        StopExecutor(task_executor);
        return false;
    }

    TcpListenOptions server_config;
    server_config.address.ip = options_.listen_ip;
    server_config.address.port = options_.listen_port;
    server_config.max_connections = options_.max_connections;
    server_config.send_queue_capacity = options_.send_queue_capacity;
    TcpCallbacks callbacks;
    callbacks.user = this;
    callbacks.on_accept = &HttpServiceImpl::HandleAccept;
    callbacks.on_read = &HttpServiceImpl::HandleRead;
    callbacks.on_close = &HttpServiceImpl::HandleClose;
    TcpServerId server =
        dependencies_.net_engine->ListenTcp(server_config, callbacks);
    if (server == 0) {
        StopExecutor(config_apply_executor);
        StopExecutor(control_executor);
        StopExecutor(stream_executor);
        StopExecutor(task_executor);
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        tcp_server_id_ = server;
        started_ = true;
    }
    INFRA_LOG_INFO(kHttpModuleName,
                   "HTTP listen %s:%u static_root=%s workers=%u "
                   "stream_workers=%u control_workers=%u config_workers=%u",
                   options_.listen_ip.c_str(),
                   static_cast<unsigned>(options_.listen_port),
                   options_.static_root.c_str(),
                   static_cast<unsigned>(options_.executor_worker_count),
                   static_cast<unsigned>(
                       options_.stream_executor_worker_count),
                   static_cast<unsigned>(
                       options_.control_executor_worker_count),
                   static_cast<unsigned>(
                       options_.config_apply_worker_count));
    return true;
}

void HttpServiceImpl::Stop() {
    TcpServerId server_id = 0;
    NetEngine *net_engine = nullptr;
    infra::Executor *task_executor = nullptr;
    infra::Executor *stream_executor = nullptr;
    infra::Executor *control_executor = nullptr;
    infra::Executor *config_apply_executor = nullptr;
    std::vector<StreamFlvClientId> flv_client_ids;
    std::vector<ConnectionId> connection_ids;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!started_) {
            return;
        }
        started_ = false;
        server_id = tcp_server_id_;
        tcp_server_id_ = 0;
        net_engine = dependencies_.net_engine;
        flv_client_ids = connections_.TakeAllFlvClients();
        connection_ids = connections_.ConnectionIds();
        connections_.Clear();
        stats_.active_connections = 0;
        task_executor = task_executor_.get();
        stream_executor = stream_executor_.get();
        control_executor = control_executor_.get();
        config_apply_executor = config_apply_executor_.get();
    }
    INFRA_LOG_INFO(kHttpModuleName, "HTTP stop begin server=%llu streams=%zu",
                   static_cast<unsigned long long>(server_id),
                   flv_client_ids.size());
    DetachFlvClients(flv_client_ids);
    if (net_engine != nullptr && server_id != 0) {
        (void)net_engine->CloseTcp(server_id);
    }
    if (net_engine != nullptr) {
        for (ConnectionId connection_id : connection_ids) {
            (void)net_engine->Close(connection_id);
        }
    }
    StopExecutor(config_apply_executor);
    StopExecutor(control_executor);
    StopExecutor(stream_executor);
    StopExecutor(task_executor);
    INFRA_LOG_INFO(kHttpModuleName, "HTTP stopped");
}

void HttpServiceImpl::Release() {
    Stop();
    std::lock_guard<std::mutex> guard(mutex_);
    connections_.Clear();
    task_executor_.reset();
    stream_executor_.reset();
    control_executor_.reset();
    config_apply_executor_.reset();
    initialized_ = false;
}

HttpResponse HttpServiceImpl::HandleRequest(const HttpRequest &request) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!initialized_) {
            return StatusResponse(500, "Service not initialized");
        }
        ++stats_.total_requests;
    }

    if (request.path.empty() || request.path[0] != '/') {
        IncrementParseFailures();
        return StatusResponse(400, "Invalid request path");
    }

    const HttpRouteMatch route = MatchHttpRoute(request);
    if (route.found && route.handler != nullptr) {
        return route.handler(this, request);
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

HttpServiceStats HttpServiceImpl::GetStats() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return stats_;
}

HttpListenAddress HttpServiceImpl::LocalAddress() const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (dependencies_.net_engine == nullptr || tcp_server_id_ == 0) {
        return HttpListenAddress{};
    }
    NetAddress address =
        dependencies_.net_engine->TcpLocalAddress(tcp_server_id_);
    HttpListenAddress result;
    result.ip = address.ip;
    result.port = address.port;
    return result;
}

void HttpServiceImpl::HandleAccept(void *user, ConnectionId id, NetAddress peer) {
    HttpServiceImpl *self = static_cast<HttpServiceImpl *>(user);
    if (self != nullptr) {
        self->OnConnection(id, std::move(peer));
    }
}

void HttpServiceImpl::HandleRead(void *user, ConnectionId id, const uint8_t *data,
                                 size_t size) {
    HttpServiceImpl *self = static_cast<HttpServiceImpl *>(user);
    if (self != nullptr) {
        self->OnMessage(id, data, static_cast<uint32_t>(size));
    }
}

void HttpServiceImpl::HandleClose(void *user, ConnectionId id) {
    HttpServiceImpl *self = static_cast<HttpServiceImpl *>(user);
    if (self != nullptr) {
        self->OnClose(id);
    }
}

bool HttpServiceImpl::IsConfigMutationRequest(const HttpRequest &request) {
    return request.method == HttpMethod::kPut &&
           StartsWith(request.path, "/api/config/");
}

bool HttpServiceImpl::IsStreamRequest(const HttpRequest &request) {
    if (request.method == HttpMethod::kGet) {
        return StartsWith(request.path, "/api/flv/") ||
               StartsWith(request.path, "/api/hls/") ||
               StartsWith(request.path, "/api/snapshot/");
    }
    return StartsWith(request.path, "/api/webrtc") &&
           (request.method == HttpMethod::kPost ||
            request.method == HttpMethod::kDelete);
}

bool HttpServiceImpl::IsControlMutationRequest(const HttpRequest &request) {
    if (request.method == HttpMethod::kGet ||
        !StartsWith(request.path, "/api/")) {
        return false;
    }
    return StartsWith(request.path, "/api/network/") ||
           StartsWith(request.path, "/api/system/") ||
           StartsWith(request.path, "/api/time/") ||
           StartsWith(request.path, "/api/upgrade/");
}

infra::Executor *HttpServiceImpl::ExecutorForRequestLocked(
    const HttpRequest &request) const {
    if (IsConfigMutationRequest(request)) {
        return config_apply_executor_.get();
    }
    if (IsStreamRequest(request)) {
        return stream_executor_.get();
    }
    if (IsControlMutationRequest(request)) {
        return control_executor_.get();
    }
    return task_executor_.get();
}

void HttpServiceImpl::DetachFlvClient(StreamFlvClientId client_id) {
    if (client_id != 0 && dependencies_.stream_hub_service != nullptr) {
        (void)dependencies_.stream_hub_service->DetachFlvClient(client_id);
    }
}

void HttpServiceImpl::DetachFlvClients(
    const std::vector<StreamFlvClientId> &client_ids) {
    if (dependencies_.stream_hub_service == nullptr) {
        return;
    }
    for (StreamFlvClientId client_id : client_ids) {
        if (client_id != 0) {
            (void)dependencies_.stream_hub_service->DetachFlvClient(client_id);
        }
    }
}

bool HttpServiceImpl::EnqueueStreamingChunk(ConnectionId connection_id,
                                            const uint8_t *data, size_t size) {
    NetEngine *net_engine = nullptr;
    if (data == nullptr || size == 0) {
        return true;
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!connections_.IsStreaming(connection_id)) {
            return false;
        }
        net_engine = dependencies_.net_engine;
    }
    if (net_engine == nullptr) {
        return false;
    }
    if (net_engine->PendingBytes(connection_id) >= kMaxStreamingQueuedBytes) {
        (void)net_engine->Close(connection_id);
        return false;
    }
    if (!net_engine->Send(connection_id, data, size)) {
        INFRA_LOG_ERROR(kHttpModuleName, "HTTP-FLV send failed conn=%llu size=%zu",
                        static_cast<unsigned long long>(connection_id),
                        size);
        (void)net_engine->Close(connection_id);
        return false;
    }
    return true;
}

bool HttpServiceImpl::TryHandleStreamingRequest(
    ConnectionId connection_id, const HttpRequest &request) {
    if (!StartsWith(request.path, "/api/flv/") ||
        request.method != HttpMethod::kGet) {
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        ++stats_.total_requests;
    }
    INFRA_LOG_INFO(kHttpModuleName, "HTTP-FLV request conn=%llu path=%s peer=%s",
                   static_cast<unsigned long long>(connection_id),
                   request.path.c_str(), request.client_ip.c_str());
    http_handlers::StartFlvStream(this, connection_id, request);
    return true;
}

const HttpServiceDependencies &HttpServiceImpl::Dependencies() const {
    return dependencies_;
}

void HttpServiceImpl::IncrementParseFailures() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.parse_failures;
}

void HttpServiceImpl::IncrementNotFound() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.not_found;
}

void HttpServiceImpl::IncrementAuthFailures() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.auth_failures;
}

void HttpServiceImpl::IncrementPermissionDenied() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.permission_denied;
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
    TokenValidationResult validated =
        dependencies_.auth_service->ValidateToken(token);
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
    if (!dependencies_.auth_service->CheckPermission(authenticated, permission,
                                                     target)) {
        IncrementPermissionDenied();
        RecordOperation(request, authenticated,
                        OperationAction::kPermissionDenied, target,
                        OperationResult::kRejected, "permission_denied");
        return false;
    }
    if (principal != nullptr) {
        *principal = authenticated;
    }
    return true;
}

void HttpServiceImpl::RecordOperation(
    const HttpRequest &request, const AuthPrincipal &principal,
    OperationAction action, const std::string &target, OperationResult result,
    const std::string &reason) {
    if (dependencies_.logger_service == nullptr) {
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
    (void)dependencies_.logger_service->RecordOperation(record);
}

bool HttpServiceImpl::BeginFlvSession(
    ConnectionId connection_id, const std::shared_ptr<IStreamFlvSink> &sink) {
    std::lock_guard<std::mutex> guard(mutex_);
    return connections_.BeginFlvStream(connection_id, sink);
}

bool HttpServiceImpl::AttachFlvSessionClient(ConnectionId connection_id,
                                             StreamFlvClientId client_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    return connections_.AttachFlvClient(connection_id, client_id);
}

void HttpServiceImpl::CloseConnection(ConnectionId connection_id) {
    NetEngine *net_engine = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        net_engine = dependencies_.net_engine;
    }
    if (net_engine != nullptr) {
        (void)net_engine->Close(connection_id);
    }
}

HttpResponse HttpServiceImpl::HandleStaticFile(const HttpRequest &request) {
    const StaticFileResult result =
        BuildStaticFileResponse(request, options_.static_root);
    if (result.status == StaticFileStatus::kNotFound) {
        IncrementNotFound();
        return StatusResponse(404, "Not Found");
    }
    if (result.status == StaticFileStatus::kForbidden) {
        return StatusResponse(403, "Forbidden");
    }
    return result.response;
}

void HttpServiceImpl::OnConnection(ConnectionId connection_id, NetAddress peer) {
    std::string peer_ip = peer.ip;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        connections_.Add(connection_id, std::move(peer.ip));
        ++stats_.active_connections;
    }
    INFRA_LOG_INFO(kHttpModuleName, "HTTP accept conn=%llu peer=%s",
                   static_cast<unsigned long long>(connection_id),
                   peer_ip.c_str());
    ArmConnectionTimer(connection_id, options_.request_timeout_ms);
}

void HttpServiceImpl::OnClose(ConnectionId connection_id) {
    ClosedHttpConnectionInfo closed;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        closed = connections_.Remove(connection_id);
        if (!closed.found) {
            return;
        }
        if (stats_.active_connections > 0) {
            --stats_.active_connections;
        }
    }
    DetachFlvClient(closed.flv_client_id);
    INFRA_LOG_INFO(kHttpModuleName, "HTTP close conn=%llu streaming=%d flv=%llu",
                   static_cast<unsigned long long>(connection_id),
                   closed.was_streaming ? 1 : 0,
                   static_cast<unsigned long long>(closed.flv_client_id));
}

void HttpServiceImpl::OnMessage(ConnectionId connection_id, const uint8_t *data,
                                uint32_t size) {
    if (data == nullptr) {
        return;
    }
    HttpConnectionParseResult parsed;
    std::vector<HttpConnectionRequestLog> request_logs;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!connections_.AppendRequestBytes(connection_id, data, size)) {
            return;
        }
        parsed = connections_.ParsePendingRequests(
            connection_id, MakeConnectionParseOptions(), &request_logs);
    }
    LogRequests(request_logs);

    if (!parsed.success) {
        IncrementParseFailures();
        SendResponse(connection_id, ParseFailureResponse(parsed.failure),
                     true);
        return;
    }
    ArmConnectionTimer(connection_id, options_.request_timeout_ms);
    TryPostNextRequest(connection_id);
}

void HttpServiceImpl::TryPostNextRequest(ConnectionId connection_id) {
    PendingHttpRequest pending;
    infra::Executor *task_executor = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!connections_.TakeNextRequest(connection_id, &pending)) {
            return;
        }
        task_executor = ExecutorForRequestLocked(pending.request);
    }
    if (task_executor == nullptr ||
        task_executor->Post([this, connection_id,
                             pending = std::move(pending)]() mutable {
            if (TryHandleStreamingRequest(connection_id, pending.request)) {
                return;
            }
            HttpResponse handled = HandleRequest(pending.request);
            SendResponse(connection_id, handled, pending.close_after_response);
        }) == false) {
        SendResponse(connection_id, StatusResponse(503, "Service Unavailable"),
                     true);
    }
}

void HttpServiceImpl::SendResponseAndClose(ConnectionId connection_id,
                                           const HttpResponse &response) {
    SendResponse(connection_id, response, true);
}

void HttpServiceImpl::SendResponse(ConnectionId connection_id,
                                   const HttpResponse &response,
                                   bool close_after_response) {
    NetEngine *net_engine = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        net_engine = dependencies_.net_engine;
    }
    if (net_engine == nullptr) {
        return;
    }
    HttpResponse response_copy = response;
    response_copy.headers["Connection"] =
        close_after_response ? "close" : "keep-alive";
    const std::string serialized = SerializeResponse(response_copy);
    if (!net_engine->Send(connection_id,
                          reinterpret_cast<const uint8_t *>(serialized.data()),
                          serialized.size())) {
        (void)net_engine->Close(connection_id);
        return;
    }
    if (close_after_response) {
        (void)net_engine->CloseAfterSend(connection_id);
        return;
    }
    CompleteKeepAliveRequest(connection_id);
}

void HttpServiceImpl::CompleteKeepAliveRequest(ConnectionId connection_id) {
    HttpConnectionParseResult parsed;
    std::vector<HttpConnectionRequestLog> request_logs;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        parsed = connections_.CompleteKeepAliveRequest(
            connection_id, MakeConnectionParseOptions(), &request_logs);
    }
    if (!parsed.found) {
        return;
    }
    LogRequests(request_logs);
    if (!parsed.success) {
        IncrementParseFailures();
        SendResponse(connection_id, ParseFailureResponse(parsed.failure),
                     true);
        return;
    }
    if (parsed.has_pending) {
        TryPostNextRequest(connection_id);
        return;
    }
    ArmConnectionTimer(connection_id, options_.connection_idle_timeout_ms);
}

HttpConnectionParseOptions HttpServiceImpl::MakeConnectionParseOptions() const {
    HttpConnectionParseOptions options;
    options.max_request_header_bytes = options_.max_request_header_bytes;
    options.max_request_body_bytes = options_.max_request_body_bytes;
    options.max_pipelined_requests = options_.max_pipelined_requests;
    options.max_requests_per_connection =
        options_.max_requests_per_connection;
    options.enable_keep_alive = options_.enable_keep_alive;
    return options;
}

HttpResponse HttpServiceImpl::ParseFailureResponse(
    HttpConnectionParseFailure failure) {
    if (failure == HttpConnectionParseFailure::kPayloadTooLarge) {
        return StatusResponse(413, "Payload Too Large");
    }
    return StatusResponse(400, "Bad Request");
}

void HttpServiceImpl::LogRequests(
    const std::vector<HttpConnectionRequestLog> &request_logs) {
    for (const HttpConnectionRequestLog &log : request_logs) {
        INFRA_LOG_INFO(kHttpModuleName,
                       "HTTP request conn=%llu peer=%s %s %s query=%zu body=%zu",
                       static_cast<unsigned long long>(log.connection_id),
                       log.client_ip.c_str(), HttpMethodName(log.method),
                       log.path.c_str(), log.query_size, log.body_size);
    }
}

void HttpServiceImpl::ArmConnectionTimer(ConnectionId connection_id,
                                      uint32_t delay_ms) {
    NetEngine *net_engine = nullptr;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!connections_.ArmTimer(connection_id, &generation)) {
            return;
        }
        net_engine = dependencies_.net_engine;
    }
    if (net_engine == nullptr) {
        return;
    }
    (void)net_engine->RunOnIoAfter(
        delay_ms, [this, connection_id, generation]() {
            NetEngine *engine = nullptr;
            bool should_close = false;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                should_close =
                    connections_.IsTimerCurrent(connection_id, generation);
                engine = dependencies_.net_engine;
            }
            if (should_close && engine != nullptr) {
                (void)engine->Close(connection_id);
            }
        });
}

std::unique_ptr<IHttpService>
CreateHttpService(const HttpServiceOptions &options,
                  const HttpServiceDependencies &dependencies) {
    return std::unique_ptr<IHttpService>(
        new HttpServiceImpl(options, dependencies));
}

}  // namespace live_stream
