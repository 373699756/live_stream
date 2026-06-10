#include "http_server.h"

#include "http_handler_utils.h"
#include "http_protocol.h"
#include "http_static_files.h"
#include "infra/executor.h"
#include "infra/log.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

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

HttpServer::HttpServer(const HttpOptions &options,
                       const HttpDependencies &dependencies,
                       HttpRequestHandler *request_handler)
    : options_(options),
      connection_writer_(options.send_buffer_limit_bytes),
      net_engine_(dependencies.net_engine),
      net_executor_(dependencies.net_executor),
      request_handler_(request_handler) {}

HttpServer::~HttpServer() {
    Stop();
    Release();
}

bool HttpServer::Prepare() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (initialized_) {
        return true;
    }
    if (net_engine_ == nullptr || request_handler_ == nullptr) {
        return false;
    }
    if (net_executor_ == nullptr) {
        return false;
    }
    if (options_.max_request_header_bytes == 0 ||
        options_.max_request_body_bytes == 0 || options_.max_connections == 0 ||
        options_.request_timeout_ms == 0 ||
        options_.connection_idle_timeout_ms == 0 ||
        options_.send_buffer_limit_bytes == 0 ||
        options_.max_requests_per_connection == 0 ||
        options_.max_pipelined_requests == 0 ||
        options_.stream_executor_worker_count == 0 ||
        options_.stream_executor_queue_capacity == 0 ||
        options_.control_executor_worker_count == 0 ||
        options_.control_executor_queue_capacity == 0) {
        return false;
    }
    stream_executor_.reset(new infra::Executor());
    control_executor_.reset(new infra::Executor());
    initialized_ = true;
    return true;
}

bool HttpServer::Start() {
    if (!Prepare()) {
        Error(kHttpModuleName, "HTTP server prepare failed");
        return false;
    }
    infra::Executor *stream_executor = nullptr;
    infra::Executor *control_executor = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (started_) {
            Info(kHttpModuleName,
                           "HTTP server start skipped: already started");
            return true;
        }
        if (net_engine_ == nullptr) {
            Error(kHttpModuleName,
                            "HTTP server start failed: net engine null");
            return false;
        }
        stream_executor = stream_executor_.get();
        control_executor = control_executor_.get();
    }
    // 媒体长连接和控制 API 分开执行：/live、SSE、WHEP 可能长时间写 socket，
    // 不能占住修改配置、登录等控制请求的 worker。
    if (!StartExecutor(stream_executor, options_.stream_executor_worker_count,
                       options_.stream_executor_queue_capacity)) {
        Error(kHttpModuleName,
                        "HTTP stream executor start failed");
        return false;
    }
    if (!StartExecutor(control_executor, options_.control_executor_worker_count,
                       options_.control_executor_queue_capacity)) {
        Error(kHttpModuleName,
                        "HTTP control executor start failed");
        StopExecutor(stream_executor);
        return false;
    }

    TcpListenOptions server_config;
    server_config.address.ip = options_.listen_ip;
    server_config.address.port = options_.listen_port;
    server_config.owner_protocol = kHttpModuleName;
    server_config.max_connections = options_.max_connections;
    server_config.send_queue_capacity = options_.send_queue_capacity;
    server_config.send_buffer_limit_bytes = options_.send_buffer_limit_bytes;
    server_config.send_stall_timeout_ms = options_.connection_idle_timeout_ms;
    server_config.write_timeout_ms = options_.connection_idle_timeout_ms;
    TcpCallbacks callbacks;
    callbacks.user = this;
    callbacks.on_accept = &HttpServer::HandleAccept;
    callbacks.on_read = &HttpServer::HandleRead;
    callbacks.on_close = &HttpServer::HandleClose;
    TcpServerId server = net_engine_->ListenTcp(net_executor_, server_config,
                                                callbacks);
    if (server == 0) {
        Error(kHttpModuleName, "HTTP listen tcp failed");
        StopExecutor(control_executor);
        StopExecutor(stream_executor);
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        tcp_server_id_ = server;
        started_ = true;
    }
    const std::vector<StaticAssetStatus> static_assets = CheckStaticAssets(
        options_.static_root,
        std::vector<std::string>{"index.html", "vendor/flv.min.js",
                                 "vendor/hls.min.js"});
    for (const StaticAssetStatus &asset : static_assets) {
        if (!asset.exists || asset.size == 0) {
            Error(
                kHttpModuleName,
                "HTTP static asset missing relative=%s path=%s exists=%d "
                "size=%llu",
                asset.relative_path.c_str(), asset.path.c_str(),
                asset.exists ? 1 : 0,
                static_cast<unsigned long long>(asset.size));
        }
    }
    return true;
}

void HttpServer::Stop() {
    TcpServerId server_id = 0;
    INetEngine *net_engine = nullptr;
    INetExecutor *net_executor = nullptr;
    infra::Executor *stream_executor = nullptr;
    infra::Executor *control_executor = nullptr;
    std::vector<HttpMediaClientHandle> media_clients;
    std::vector<ConnectionId> connection_ids;
    std::vector<NetTimerId> timer_ids;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!started_) {
            return;
        }
        started_ = false;
        server_id = tcp_server_id_;
        tcp_server_id_ = 0;
        net_engine = net_engine_;
        net_executor = net_executor_;
        // Stop() 先摘出 session 状态，再在锁外通知媒体模块 detach client。
        // close callback 可能回到 media_source/event，不能拿着 HTTP 锁跨模块调用。
        for (const auto &item : sessions_) {
            connection_ids.push_back(item.first);
            if (item.second != nullptr) {
                const NetTimerId timer_id = item.second->CancelTimeout();
                if (timer_id != 0) {
                    timer_ids.push_back(timer_id);
                }
                const HttpMediaClientHandle media_client =
                    item.second->TakeMediaClient();
                if (media_client.id != 0) {
                    media_clients.push_back(media_client);
                }
            }
        }
        sessions_.clear();
        stats_.active_connections = 0;
        stream_executor = stream_executor_.get();
        control_executor = control_executor_.get();
    }
    Info(kHttpModuleName, "HTTP stop begin server=%llu streams=%zu",
                   static_cast<unsigned long long>(server_id),
                   media_clients.size());
    NotifyStreamsClosed(media_clients);
    if (net_engine != nullptr) {
        for (NetTimerId timer_id : timer_ids) {
            CancelNetTimer(net_executor, timer_id);
        }
    }
    if (net_engine != nullptr && server_id != 0) {
        (void)net_engine->CloseTcp(server_id);
    }
    if (net_engine != nullptr) {
        // 主动关闭所有连接可以触发 net close path，但 sessions_ 已经在上面摘除，
        // 所以 OnClose() 不会重复 detach media client。
        for (ConnectionId connection_id : connection_ids) {
            (void)net_engine->Close(connection_id);
        }
    }
    StopExecutor(control_executor);
    StopExecutor(stream_executor);
    Info(kHttpModuleName, "HTTP stopped");
}

void HttpServer::Release() {
    Stop();
    std::lock_guard<std::mutex> guard(mutex_);
    sessions_.clear();
    stream_executor_.reset();
    control_executor_.reset();
    initialized_ = false;
}

HttpListenAddress HttpServer::LocalAddress() const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (net_engine_ == nullptr || tcp_server_id_ == 0) {
        return HttpListenAddress{};
    }
    NetAddress address = net_engine_->TcpLocalAddress(tcp_server_id_);
    HttpListenAddress result;
    result.ip = address.ip;
    result.port = address.port;
    return result;
}

HttpStats HttpServer::GetStats() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return stats_;
}

std::vector<HttpStreamingSessionDiagnostics>
HttpServer::GetStreamingSessionDiagnostics() const {
    INetEngine *net_engine = nullptr;
    std::vector<HttpSessionStreamingInfo> sessions;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        net_engine = net_engine_;
        sessions.reserve(sessions_.size());
        for (const auto &item : sessions_) {
            if (item.second != nullptr && item.second->is_streaming()) {
                const HttpSessionStreamingInfo info =
                    item.second->StreamingInfo();
                if (info.media_type != HttpMediaClientType::kNone) {
                    sessions.push_back(info);
                }
            }
        }
    }
    if (net_engine == nullptr) {
        return std::vector<HttpStreamingSessionDiagnostics>();
    }

    std::vector<HttpStreamingSessionDiagnostics> diagnostics;
    diagnostics.reserve(sessions.size());
    for (const HttpSessionStreamingInfo &session : sessions) {
        diagnostics.push_back(BuildStreamingDiagnostics(
            session,
            net_engine->GetConnectionDiagnostics(session.connection_id)));
    }
    return diagnostics;
}

void HttpServer::IncrementTotalRequests() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.total_requests;
}

void HttpServer::IncrementParseFailures() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.parse_failures;
}

void HttpServer::IncrementNotFound() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.not_found;
}

void HttpServer::IncrementAuthFailures() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.auth_failures;
}

void HttpServer::IncrementPermissionDenied() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.permission_denied;
}

void HttpServer::SendResponse(ConnectionId connection_id,
                              const HttpResponse &response,
                              bool close_after_response) {
    const bool sent = connection_writer_.SendResponse(
        net_engine_, connection_id, response, close_after_response);
    if (sent && !close_after_response) {
        CompleteKeepAliveRequest(connection_id);
    }
}

bool HttpServer::BeginStream(ConnectionId connection_id,
                             HttpMediaClientType type,
                             StreamId stream_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto iter = sessions_.find(connection_id);
    // BeginStream 是普通 HTTP request 到媒体长连接的单向状态迁移；
    // 失败通常说明连接已经被关闭或已进入 streaming。
    return iter != sessions_.end() && iter->second != nullptr &&
           iter->second->BeginStream(type, stream_id);
}

bool HttpServer::AttachStreamClient(ConnectionId connection_id,
                                    HttpMediaClientHandle client) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto iter = sessions_.find(connection_id);
    return iter != sessions_.end() && iter->second != nullptr &&
           iter->second->AttachStreamClient(client);
}

bool HttpServer::EnqueueStreamingChunk(ConnectionId connection_id,
                                       const uint8_t *data, size_t size) {
    if (data == nullptr || size == 0) {
        return true;
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter == sessions_.end() || iter->second == nullptr ||
            !iter->second->is_streaming()) {
            Error(kHttpModuleName,
                  "HTTP stream enqueue reject conn=%llu reason=closed",
                  static_cast<unsigned long long>(connection_id));
            return false;
        }
    }
    const bool enqueued = connection_writer_.EnqueueStreamingChunk(
        net_engine_, connection_id, data, size);
    if (!enqueued) {
        (void)MarkStreamingClosing(connection_id);
    }
    return enqueued;
}

bool HttpServer::EnqueueStreamingSlices(ConnectionId connection_id,
                                        const MediaSlice *slices,
                                        size_t slice_count) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter == sessions_.end() || iter->second == nullptr ||
            !iter->second->is_streaming()) {
            Error(kHttpModuleName,
                  "HTTP stream enqueue reject conn=%llu reason=closed",
                  static_cast<unsigned long long>(connection_id));
            return false;
        }
    }
    const bool enqueued = connection_writer_.EnqueueStreamingSlices(
        net_engine_, connection_id, slices, slice_count);
    if (!enqueued) {
        (void)MarkStreamingClosing(connection_id);
    }
    return enqueued;
}

void HttpServer::SetCloseCallback(HttpMediaCloseCallback callback) {
    std::lock_guard<std::mutex> guard(mutex_);
    close_callback_ = std::move(callback);
}

void HttpServer::CloseConnection(ConnectionId connection_id) {
    CloseConnectionWithReason(connection_id, TcpCloseReason::kNormal);
}

void HttpServer::CloseConnectionWithReason(ConnectionId connection_id,
                                           TcpCloseReason reason) {
    INetEngine *net_engine = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter != sessions_.end() && iter->second != nullptr) {
            (void)iter->second->MarkStreamClosing();
        }
        net_engine = net_engine_;
    }
    if (net_engine != nullptr) {
        connection_writer_.CloseConnection(net_engine, connection_id, reason);
    }
}

void HttpServer::HandleAccept(void *user, ConnectionId id, NetAddress peer) {
    HttpServer *self = static_cast<HttpServer *>(user);
    if (self != nullptr) {
        self->OnConnection(id, std::move(peer));
    }
}

void HttpServer::HandleRead(void *user, ConnectionId id, const uint8_t *data,
                            size_t size) {
    HttpServer *self = static_cast<HttpServer *>(user);
    if (self != nullptr) {
        self->OnMessage(id, data, static_cast<uint32_t>(size));
    }
}

void HttpServer::HandleClose(void *user, ConnectionId id,
                             TcpCloseReason reason) {
    HttpServer *self = static_cast<HttpServer *>(user);
    if (self != nullptr) {
        self->OnClose(id, reason);
    }
}

infra::Executor *HttpServer::ExecutorForRequestLocked(
    const HttpRequest &request) const {
    if (request_handler_ != nullptr &&
        request_handler_->ShouldUseStreamExecutor(request)) {
        return stream_executor_.get();
    }
    return control_executor_.get();
}

void HttpServer::NotifyStreamClosed(const HttpMediaClientHandle &client) {
    HttpMediaCloseCallback close_callback;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        close_callback = close_callback_;
    }
    if (client.id != 0 && close_callback) {
        close_callback(client);
    }
}

void HttpServer::NotifyStreamsClosed(
    const std::vector<HttpMediaClientHandle> &clients) {
    for (const HttpMediaClientHandle &client : clients) {
        NotifyStreamClosed(client);
    }
}

bool HttpServer::MarkStreamingClosing(ConnectionId connection_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto iter = sessions_.find(connection_id);
    return iter != sessions_.end() && iter->second != nullptr &&
           iter->second->MarkStreamClosing();
}

bool HttpServer::HandleStreamingRequestResult(
    ConnectionId connection_id, const HttpRequest &request,
    HttpStreamingRequestResult result) {
    switch (result) {
        case HttpStreamingRequestResult::kNotHandled:
            return false;
        case HttpStreamingRequestResult::kResponseSent:
            Debug(kHttpModuleName,
                  "HTTP streaming request response sent conn=%llu path=%s",
                  static_cast<unsigned long long>(connection_id),
                  request.path.c_str());
            return true;
        case HttpStreamingRequestResult::kStreaming:
            Debug(kHttpModuleName,
                  "HTTP streaming request attached conn=%llu path=%s",
                  static_cast<unsigned long long>(connection_id),
                  request.path.c_str());
            return true;
        case HttpStreamingRequestResult::kClosed:
            (void)MarkStreamingClosing(connection_id);
            Debug(kHttpModuleName,
                  "HTTP streaming request closed conn=%llu path=%s",
                  static_cast<unsigned long long>(connection_id),
                  request.path.c_str());
            return true;
        case HttpStreamingRequestResult::kFailed:
            Warn(kHttpModuleName,
                 "HTTP streaming request failed conn=%llu path=%s",
                 static_cast<unsigned long long>(connection_id),
                 request.path.c_str());
            CloseConnectionWithReason(connection_id,
                                      TcpCloseReason::kInternalError);
            return true;
    }
    return true;
}

void HttpServer::OnConnection(ConnectionId connection_id, NetAddress peer) {
    std::string peer_ip = peer.ip;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        sessions_[connection_id].reset(
            new HttpSession(connection_id, std::move(peer.ip)));
        ++stats_.active_connections;
    }
    Info(kHttpModuleName, "HTTP accept conn=%llu peer=%s",
                   static_cast<unsigned long long>(connection_id),
                   peer_ip.c_str());
    ArmConnectionTimer(connection_id, options_.request_timeout_ms);
}

void HttpServer::OnClose(ConnectionId connection_id, TcpCloseReason reason) {
    ClosedHttpSessionInfo closed;
    INetExecutor *net_executor = nullptr;
    NetTimerId timer_id = 0;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter == sessions_.end() || iter->second == nullptr) {
            return;
        }
        timer_id = iter->second->CancelTimeout();
        closed = iter->second->Close();
        sessions_.erase(iter);
        if (stats_.active_connections > 0) {
            --stats_.active_connections;
        }
        net_executor = net_executor_;
    }
    // TCP close 是所有媒体长连接的最终回收点；无论是浏览器断开、超时还是队列满，
    // 都必须在这里通知 http_media 解除 FLV/MJPEG/SSE 订阅。
    CancelNetTimer(net_executor, timer_id);
    NotifyStreamClosed(closed.media_client);
    Info(kHttpModuleName,
                   "HTTP close conn=%llu reason=%d streaming=%d media_type=%d "
                   "client=%llu",
                   static_cast<unsigned long long>(connection_id),
                   static_cast<int>(reason),
                   closed.was_streaming ? 1 : 0,
                   static_cast<int>(closed.media_client.type),
                   static_cast<unsigned long long>(closed.media_client.id));
}

void HttpServer::OnMessage(ConnectionId connection_id, const uint8_t *data,
                           uint32_t size) {
    if (data == nullptr) {
        return;
    }
    HttpSessionParseResult parsed;
    std::vector<HttpRequestLog> request_logs;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter == sessions_.end() || iter->second == nullptr ||
            !iter->second->AppendRequestBytes(data, size)) {
            return;
        }
        parsed = iter->second->ParsePendingRequests(
            MakeConnectionParseOptions(), &request_logs);
    }
    LogRequests(request_logs);

    if (!parsed.success) {
        IncrementParseFailures();
        // HTTP parser 失败后只发送短错误响应并关闭，不把半包继续留在 session。
        SendResponse(connection_id, ParseFailureResponse(parsed.failure),
                     true);
        return;
    }
    ArmConnectionTimer(connection_id, options_.request_timeout_ms);
    TryPostNextRequest(connection_id);
}

void HttpServer::TryPostNextRequest(ConnectionId connection_id) {
    PendingHttpRequest pending;
    infra::Executor *executor = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter == sessions_.end() || iter->second == nullptr ||
            !iter->second->TakeNextRequest(&pending)) {
            return;
        }
        executor = ExecutorForRequestLocked(pending.request);
    }
    if (executor == nullptr ||
        executor->Post([this, connection_id,
                        pending = std::move(pending)]() mutable {
            if (request_handler_ != nullptr) {
                const HttpStreamingRequestResult stream_result =
                    request_handler_->HandleStreamingHttpRequest(
                        connection_id, pending.request);
                if (HandleStreamingRequestResult(
                        connection_id, pending.request, stream_result)) {
                    return;
                }
            }
            if (request_handler_ == nullptr) {
                SendResponse(
                    connection_id,
                    AddJsonEnvelope(
                        pending.request,
                        StatusResponse(503, "Service Unavailable")),
                    true);
                return;
            }
            HttpResponse handled =
                request_handler_->HandleHttpRequest(pending.request);
            SendResponse(connection_id, handled, pending.close_after_response);
        }) == false) {
        SendResponse(connection_id,
                     AddJsonEnvelope(
                         pending.request,
                         StatusResponse(503, "Service Unavailable")),
                     true);
    }
}

void HttpServer::CompleteKeepAliveRequest(ConnectionId connection_id) {
    HttpSessionParseResult parsed;
    std::vector<HttpRequestLog> request_logs;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter == sessions_.end() || iter->second == nullptr) {
            return;
        }
        parsed = iter->second->CompleteKeepAliveRequest(
            MakeConnectionParseOptions(), &request_logs);
    }
    LogRequests(request_logs);
    if (!parsed.success) {
        IncrementParseFailures();
        // keep-alive 后续管线请求解析失败，同样结束该 TCP 连接，避免请求边界错乱。
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

HttpSessionParseOptions HttpServer::MakeConnectionParseOptions() const {
    HttpSessionParseOptions options;
    options.max_request_header_bytes = options_.max_request_header_bytes;
    options.max_request_body_bytes = options_.max_request_body_bytes;
    options.max_pipelined_requests = options_.max_pipelined_requests;
    options.max_requests_per_connection =
        options_.max_requests_per_connection;
    options.enable_keep_alive = options_.enable_keep_alive;
    return options;
}

HttpResponse HttpServer::ParseFailureResponse(
    HttpSessionParseFailure failure) {
    if (failure == HttpSessionParseFailure::kPayloadTooLarge) {
        return StatusResponse(413, "Payload Too Large");
    }
    return StatusResponse(400, "Bad Request");
}

void HttpServer::LogRequests(
    const std::vector<HttpRequestLog> &request_logs) {
    for (const HttpRequestLog &log : request_logs) {
        Info(kHttpModuleName,
                       "HTTP request conn=%llu peer=%s %s %s query=%zu body=%zu",
                       static_cast<unsigned long long>(log.connection_id),
                       log.client_ip.c_str(), HttpMethodName(log.method),
                       log.path.c_str(), log.query_size, log.body_size);
    }
}

HttpStreamingSessionDiagnostics HttpServer::BuildStreamingDiagnostics(
    const HttpSessionStreamingInfo &session,
    const NetConnectionDiagnostics &connection) {
    HttpStreamingSessionDiagnostics diagnostics;
    diagnostics.connection_id = session.connection_id;
    diagnostics.protocol = HttpMediaClientTypeName(session.media_type);
    diagnostics.session_id = std::to_string(session.connection_id);
    diagnostics.client_id = session.media_client.id == 0
                                ? std::string()
                                : std::to_string(session.media_client.id);
    diagnostics.stream_state = HttpMediaStreamStateName(session.stream_state);
    diagnostics.stream_id = session.stream_id;
    diagnostics.client_ip = session.client_ip;
    diagnostics.pending_bytes = connection.pending_bytes;
    diagnostics.send_queue_length = connection.send_queue_length;
    diagnostics.last_write_at_ms = connection.last_write_at_ms;
    diagnostics.open = connection.connection_id == session.connection_id
                           ? connection.open
                           : session.streaming;
    if (!diagnostics.open) {
        diagnostics.close_reason = TcpCloseReasonName(connection.close_reason);
    }
    if (!connection.remote_address.ip.empty()) {
        diagnostics.remote_address = connection.remote_address.ip + ":" +
                                     std::to_string(
                                         connection.remote_address.port);
    }
    if (!connection.local_address.ip.empty()) {
        diagnostics.local_address = connection.local_address.ip + ":" +
                                    std::to_string(
                                        connection.local_address.port);
    }
    return diagnostics;
}

void HttpServer::ArmConnectionTimer(ConnectionId connection_id,
                                    uint32_t delay_ms) {
    INetEngine *net_engine = nullptr;
    INetExecutor *net_executor = nullptr;
    RenewedHttpSessionTimeout timeout;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter == sessions_.end() || iter->second == nullptr) {
            return;
        }
        timeout = iter->second->RenewTimeout();
        net_engine = net_engine_;
        net_executor = net_executor_;
    }
    if (net_engine == nullptr || net_executor == nullptr) {
        return;
    }
    // timeout_generation_ 用来淘汰旧 timer：请求推进或进入 streaming 后，
    // 旧 timer 即使晚到也不会误关新状态下的连接。
    CancelNetTimer(net_executor, timeout.replaced_timer_id);
    const NetTimerId timer_id = net_executor->RunAfter(
        delay_ms, [this, connection_id,
                   generation = timeout.generation]() {
            INetEngine *engine = nullptr;
            bool should_close = false;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                auto iter = sessions_.find(connection_id);
                should_close =
                    iter != sessions_.end() && iter->second != nullptr &&
                    iter->second->ExpireTimeout(generation);
                engine = net_engine_;
            }
            if (should_close && engine != nullptr) {
                (void)engine->Close(connection_id);
            }
        });
    if (timer_id == 0) {
        return;
    }
    bool stored = false;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        stored = iter != sessions_.end() && iter->second != nullptr &&
                 iter->second->InstallTimeout(timeout.generation, timer_id);
    }
    if (!stored) {
        CancelNetTimer(net_executor, timer_id);
    }
}

void HttpServer::CancelNetTimer(INetExecutor *executor, NetTimerId timer_id) {
    if (executor != nullptr && timer_id != 0) {
        (void)executor->CancelTimer(timer_id);
    }
}

}  // namespace live_stream
