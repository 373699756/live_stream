#include "http_server.h"

#include "http_handler_utils.h"
#include "http_protocol.h"
#include "http_static_files.h"
#include "infra/executor.h"
#include "infra/log.h"

#include <memory>
#include <string>
#include <utility>

namespace live_stream {
namespace {

void RefVideoBufferOwner(const void *owner) {
    (void)VideoBufferRef(
        const_cast<VideoBuffer*>(static_cast<const VideoBuffer*>(owner)));
}

void UnrefVideoBufferOwner(const void *owner) {
    VideoBufferUnref(
        const_cast<VideoBuffer*>(static_cast<const VideoBuffer*>(owner)));
}

NetBufferOwner VideoBufferNetOwner(VideoBuffer *buffer) {
    if (buffer == nullptr) {
        return NetBufferOwner{};
    }
    // 把 HTTP 媒体 slice 的 VideoBuffer owner 转成 net 层 owner。net 入队时 ref，
    // OutSlice 发送完成或丢弃时 unref。
    return NetBufferOwner{buffer, RefVideoBufferOwner,
                          UnrefVideoBufferOwner};
}

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
      net_engine_(dependencies.net_engine),
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
    TcpServerId server = net_engine_->ListenTcp(server_config, callbacks);
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
            CancelNetTimer(net_engine, timer_id);
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
    HttpMediaSlice body_slice;
    const HttpMediaSlice *body_slices = nullptr;
    size_t body_slice_count = 0;
    if (!response.body.empty()) {
        body_slice.data =
            reinterpret_cast<const uint8_t *>(response.body.data());
        body_slice.size = response.body.size();
        body_slices = &body_slice;
        body_slice_count = 1;
    }
    (void)SendResponseSlices(connection_id, response, body_slices,
                             body_slice_count, response.body.size(),
                             close_after_response);
}

bool HttpServer::SendResponseSlices(ConnectionId connection_id,
                                    const HttpResponse &response,
                                    const HttpMediaSlice *body_slices,
                                    size_t body_slice_count,
                                    size_t body_size,
                                    bool close_after_response) {
    INetEngine *net_engine = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        net_engine = net_engine_;
    }
    if (net_engine == nullptr) {
        return false;
    }
    if (body_slice_count > kMaxNetBufferSlices - 1 ||
        (body_slice_count != 0 && body_slices == nullptr)) {
        return false;
    }
    std::map<std::string, std::string> response_headers = response.headers;
    response_headers["Connection"] =
        close_after_response ? "close" : "keep-alive";
    HttpResponse header_response;
    header_response.status_code = response.status_code;
    header_response.headers = std::move(response_headers);
    const std::string header =
        SerializeResponseHeaderWithBodySize(header_response, body_size);
    // header 是本函数局部字符串，net 层会复制无 owner slice；媒体 body 带 owner
    // 时由 net 引用 VideoBuffer，避免 HLS segment/FLV frame 热路径复制 payload。
    NetBufferSlices slices;
    bool slices_ok = slices.Add(reinterpret_cast<const uint8_t *>(header.data()),
                                header.size());
    for (size_t i = 0; slices_ok && i < body_slice_count; ++i) {
        // 带 owner 的 body slice 不复制；无 owner 的小块会在 tcp_session 中复制到
        // inline/heap out buffer。
        slices_ok = slices.Add(body_slices[i].data, body_slices[i].size,
                               VideoBufferNetOwner(body_slices[i].owner));
    }
    if (!slices_ok || !net_engine->SendSlices(connection_id, slices)) {
        if (response.status_code >= 500) {
            Error(kHttpModuleName,
                            "HTTP response send failed conn=%llu status=%d "
                            "body=%zu header=%zu close=%d",
                            static_cast<unsigned long long>(connection_id),
                            response.status_code, body_size,
                            header.size(), close_after_response ? 1 : 0);
        } else {
            Debug(kHttpModuleName,
                            "HTTP response send failed conn=%llu status=%d "
                            "body=%zu header=%zu close=%d",
                            static_cast<unsigned long long>(connection_id),
                            response.status_code, body_size,
                            header.size(), close_after_response ? 1 : 0);
        }
        (void)net_engine->Close(connection_id);
        return false;
    }
    if (close_after_response) {
        (void)net_engine->CloseAfterSend(connection_id);
        return true;
    }
    CompleteKeepAliveRequest(connection_id);
    return true;
}

bool HttpServer::BeginStream(ConnectionId connection_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto iter = sessions_.find(connection_id);
    // BeginStream 是普通 HTTP request 到媒体长连接的单向状态迁移；
    // 失败通常说明连接已经被关闭或已进入 streaming。
    return iter != sessions_.end() && iter->second != nullptr &&
           iter->second->BeginStream();
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
    NetBufferSlices slices;
    if (data == nullptr || size == 0) {
        return true;
    }
    if (!slices.Add(data, size)) {
        return false;
    }
    return EnqueueStreamingSlices(connection_id, slices, size);
}

bool HttpServer::EnqueueStreamingSlices(ConnectionId connection_id,
                                        const HttpMediaSlice *slices,
                                        size_t slice_count) {
    NetBufferSlices net_slices;
    size_t total_size = 0;
    if (slice_count == 0) {
        return true;
    }
    if (slices == nullptr || slice_count > kMaxNetBufferSlices) {
        return false;
    }
    for (size_t i = 0; i < slice_count; ++i) {
        if (slices[i].size == 0) {
            continue;
        }
        // FLV/MJPEG 媒体 payload 带 VideoBuffer owner，HTTP header/边界字符串
        // 不带 owner 并由 net 层复制。
        if (!net_slices.Add(slices[i].data, slices[i].size,
                            VideoBufferNetOwner(slices[i].owner))) {
            return false;
        }
        total_size += slices[i].size;
    }
    if (total_size == 0) {
        return true;
    }
    return EnqueueStreamingSlices(connection_id, net_slices, total_size);
}

void HttpServer::SetCloseCallback(HttpMediaCloseCallback callback) {
    std::lock_guard<std::mutex> guard(mutex_);
    close_callback_ = std::move(callback);
}

void HttpServer::CloseConnection(ConnectionId connection_id) {
    INetEngine *net_engine = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        net_engine = net_engine_;
    }
    if (net_engine != nullptr) {
        (void)net_engine->Close(connection_id);
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

bool HttpServer::EnqueueStreamingSlices(ConnectionId connection_id,
                                        const NetBufferSlices &slices,
                                        size_t size) {
    INetEngine *net_engine = nullptr;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter == sessions_.end() || iter->second == nullptr ||
            !iter->second->is_streaming()) {
            Error(kHttpModuleName,
                            "HTTP-FLV enqueue reject conn=%llu reason=closed "
                            "size=%zu",
                            static_cast<unsigned long long>(connection_id),
                            size);
            return false;
        }
        net_engine = net_engine_;
    }
    if (net_engine == nullptr) {
        Error(kHttpModuleName,
                        "HTTP-FLV enqueue reject conn=%llu reason=no_net "
                        "size=%zu",
                        static_cast<unsigned long long>(connection_id), size);
        return false;
    }
    // 流式响应没有 Content-Length 和请求结束点，只能用 net pending bytes
    // 判定慢客户端；超限立即关连接，让媒体 reader/client 在 close callback 里回落。
    if (net_engine->PendingBytes(connection_id) >=
        options_.send_buffer_limit_bytes) {
        Error(kHttpModuleName,
                        "HTTP-FLV close conn=%llu reason=queue_full "
                        "pending=%u limit=%zu next=%zu",
                        static_cast<unsigned long long>(connection_id),
                        net_engine->PendingBytes(connection_id),
                        static_cast<size_t>(options_.send_buffer_limit_bytes),
                        size);
        (void)net_engine->Close(connection_id);
        return false;
    }
    if (!net_engine->SendSlices(connection_id, slices)) {
        Error(kHttpModuleName,
                        "HTTP-FLV send failed conn=%llu size=%zu pending=%u",
                        static_cast<unsigned long long>(connection_id),
                        size, net_engine->PendingBytes(connection_id));
        (void)net_engine->Close(connection_id);
        return false;
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
    INetEngine *net_engine = nullptr;
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
        net_engine = net_engine_;
    }
    // TCP close 是所有媒体长连接的最终回收点；无论是浏览器断开、超时还是队列满，
    // 都必须在这里通知 http_media 解除 FLV/MJPEG/SSE 订阅。
    CancelNetTimer(net_engine, timer_id);
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
            // Streaming handler 成功后会调用 BeginStream()，session 进入 streaming
            // 状态，不再回到 keep-alive parser；普通 HTTP 响应才继续解析管线请求。
            if (request_handler_ != nullptr &&
                request_handler_->HandleStreamingHttpRequest(
                    connection_id, pending.request)) {
                return;
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

void HttpServer::ArmConnectionTimer(ConnectionId connection_id,
                                    uint32_t delay_ms) {
    INetEngine *net_engine = nullptr;
    RenewedHttpSessionTimeout timeout;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto iter = sessions_.find(connection_id);
        if (iter == sessions_.end() || iter->second == nullptr) {
            return;
        }
        timeout = iter->second->RenewTimeout();
        net_engine = net_engine_;
    }
    if (net_engine == nullptr) {
        return;
    }
    // timeout_generation_ 用来淘汰旧 timer：请求推进或进入 streaming 后，
    // 旧 timer 即使晚到也不会误关新状态下的连接。
    CancelNetTimer(net_engine, timeout.replaced_timer_id);
    const NetTimerId timer_id = net_engine->RunOnIoAfter(
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
        CancelNetTimer(net_engine, timer_id);
    }
}

void HttpServer::CancelNetTimer(INetEngine *net_engine, NetTimerId timer_id) {
    if (net_engine != nullptr && timer_id != 0) {
        (void)net_engine->CancelIoTimer(timer_id);
    }
}

}  // namespace live_stream
