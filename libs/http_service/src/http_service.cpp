#include "http_service.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "ai_service.h"
#include "auth_service.h"
#include "config_service.h"
#include "stream_hub_service.h"
#include "http_protocol.h"
#include "http_request_utils.h"
#include "http_static_files.h"
#include "infra/executor.h"
#include "infra/fs.h"
#include "infra/log.h"
#include "infra/time.h"
#include "live_stream/json_utils.h"
#include "logger_service.h"
#include "media_service.h"
#include "net_service.h"
#include "network_service.h"
#include "rtsp_service.h"
#include "snapshot_service.h"
#include "system_service.h"
#include "time_service.h"
#include "upgrade_service.h"
#include "webrtc_service.h"

namespace live_stream {
namespace {

constexpr const char *kModuleName = "http_service";
constexpr const char *kUpgradeUploadDir = "/tmp/live_stream/upgrade/uploads";
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

// --- Domain: shared ---

HttpResponse JsonResponse(int status_code, const ConfigJson &value) {
    HttpResponse response;
    response.status_code = status_code;
    response.headers["Content-Type"] = "application/json";
    response.body = value.dump();
    return response;
}

HttpResponse StatusResponse(int status_code, const std::string &reason) {
    ConfigJson root = ConfigJson::object();
    root["error"] = reason;
    return JsonResponse(status_code, root);
}

HttpResponse OkResponse() {
    ConfigJson root = ConfigJson::object();
    root["ok"] = true;
    return JsonResponse(200, root);
}

std::string
BuildStreamingHeaderBlock(int status_code,
                          const std::map<std::string, std::string> &headers) {
    std::string out = "HTTP/1.1 " + std::to_string(status_code) + " OK\r\n";
    for (const auto &header : headers) {
        out += header.first + ": " + header.second + "\r\n";
    }
    out += "Connection: close\r\n";
    out += "\r\n";
    return out;
}

bool IsAiConfigEnabled(IConfigService *config_service) {
    if (config_service == nullptr) {
        return false;
    }
    ConfigJson config = config_service->GetValue("ai");
    bool enabled = false;
    return config.is_object() && json_utils::Load(config, "enabled", &enabled) &&
           enabled;
}

// --- Domain: media/video (stream id helpers, used across domains) ---

const char *StreamIdToJsonString(StreamId stream_id) {
    switch (stream_id) {
        case StreamId::kMain:
            return "main";
        case StreamId::kSub:
            return "sub";
        case StreamId::kSnapshot:
            return "snapshot";
    }
    return "unknown";
}

bool StreamIdFromJsonString(const std::string &value, StreamId *stream_id) {
    if (stream_id == nullptr) {
        return false;
    }
    if (value == "main") {
        *stream_id = StreamId::kMain;
        return true;
    }
    if (value == "sub") {
        *stream_id = StreamId::kSub;
        return true;
    }
    return false;
}

}  // namespace

class HttpServiceImpl : public IHttpService {
public:
    HttpServiceImpl(const HttpServiceOptions &options,
                    const HttpServiceDependencies &dependencies)
        : options_(options), dependencies_(dependencies) {}

    ~HttpServiceImpl() override {
        Stop();
        Release();
    }

    bool Prepare() {
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
            options_.config_apply_worker_count == 0 ||
            options_.config_apply_queue_capacity == 0) {
            return false;
        }
        task_executor_.reset(new infra::Executor());
        config_apply_executor_.reset(new infra::Executor());
        initialized_ = true;
        return true;
    }

    bool Start() override {
        if (!Prepare()) {
            return false;
        }
        infra::Executor *task_executor = nullptr;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (started_) {
                return true;
            }
            if (dependencies_.net_engine == nullptr) {
                return false;
            }
            task_executor = task_executor_.get();
        }
        infra::ExecutorOptions executor_options;
        executor_options.worker_count = options_.executor_worker_count;
        executor_options.queue_capacity = options_.executor_queue_capacity;
        if (!task_executor->Start(executor_options)) {
            return false;
        }
        infra::Executor *config_apply_executor = nullptr;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            config_apply_executor = config_apply_executor_.get();
        }
        infra::ExecutorOptions config_apply_options;
        config_apply_options.worker_count = options_.config_apply_worker_count;
        config_apply_options.queue_capacity = options_.config_apply_queue_capacity;
        if (config_apply_executor == nullptr ||
            !config_apply_executor->Start(config_apply_options)) {
            task_executor->Stop(infra::StopMode::kDiscard);
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
            config_apply_executor->Stop(infra::StopMode::kDiscard);
            task_executor->Stop(infra::StopMode::kDiscard);
            return false;
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            tcp_server_id_ = server;
            started_ = true;
        }
        INFRA_LOG_INFO(kModuleName,
                       "HTTP listen %s:%u static_root=%s workers=%u "
                       "config_workers=%u",
                       options_.listen_ip.c_str(),
                       static_cast<unsigned>(options_.listen_port),
                       options_.static_root.c_str(),
                       static_cast<unsigned>(options_.executor_worker_count),
                       static_cast<unsigned>(
                           options_.config_apply_worker_count));
        return true;
    }

    void Stop() override {
        TcpServerId server_id = 0;
        NetEngine *net_engine = nullptr;
        infra::Executor *task_executor = nullptr;
        infra::Executor *config_apply_executor = nullptr;
        std::vector<StreamFlvClientId> flv_client_ids;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!started_) {
                return;
            }
            started_ = false;
            server_id = tcp_server_id_;
            tcp_server_id_ = 0;
            net_engine = dependencies_.net_engine;
            for (const auto &item : sessions_) {
                if (item.second.flv_client_id != 0) {
                    flv_client_ids.push_back(item.second.flv_client_id);
                }
            }
            sessions_.clear();
            stats_.active_connections = 0;
            task_executor = task_executor_.get();
            config_apply_executor = config_apply_executor_.get();
        }
        INFRA_LOG_INFO(kModuleName, "HTTP stop begin server=%llu streams=%zu",
                       static_cast<unsigned long long>(server_id),
                       flv_client_ids.size());
        if (dependencies_.stream_hub_service != nullptr) {
            for (StreamFlvClientId client_id : flv_client_ids) {
                (void)dependencies_.stream_hub_service->DetachFlvClient(client_id);
            }
        }
        if (net_engine != nullptr && server_id != 0) {
            (void)net_engine->CloseTcp(server_id);
        }
        if (config_apply_executor != nullptr) {
            config_apply_executor->Stop(infra::StopMode::kDiscard);
        }
        if (task_executor != nullptr) {
            task_executor->Stop(infra::StopMode::kDiscard);
        }
        INFRA_LOG_INFO(kModuleName, "HTTP stopped");
    }

    void Release() {
        Stop();
        std::lock_guard<std::mutex> guard(mutex_);
        sessions_.clear();
        task_executor_.reset();
        config_apply_executor_.reset();
        initialized_ = false;
    }

    HttpResponse HandleRequest(const HttpRequest &request) override {
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

        if (request.path == "/api/auth/login" &&
            request.method == HttpMethod::kPost) {
            return HandleLogin(request);
        }
        if (request.path == "/api/auth/logout" &&
            request.method == HttpMethod::kPost) {
            return HandleLogout(request);
        }
        if (request.path == "/api/auth/me" && request.method == HttpMethod::kGet) {
            return HandleMe(request);
        }
        if (request.path == "/api/media/capabilities" &&
            request.method == HttpMethod::kGet) {
            return HandleMediaCapabilities();
        }
        if (request.path == "/api/status/streams" &&
            request.method == HttpMethod::kGet) {
            return HandleStreamStatus(request);
        }
        if (request.path == "/api/system/status" &&
            request.method == HttpMethod::kGet) {
            return HandleSystemStatus(request);
        }
        if (request.path == "/api/system/capabilities" &&
            request.method == HttpMethod::kGet) {
            return HandleSystemCapabilities(request);
        }
        if (request.path == "/api/system/reboot" &&
            request.method == HttpMethod::kPost) {
            return HandleSystemReboot(request);
        }
        if (request.path == "/api/system/factory-reset" &&
            request.method == HttpMethod::kPost) {
            return HandleSystemFactoryReset(request);
        }
        if (request.path == "/api/time/status" &&
            request.method == HttpMethod::kGet) {
            return HandleTimeStatus(request);
        }
        if (request.path == "/api/time/timezone" &&
            request.method == HttpMethod::kPut) {
            return HandleTimeTimezone(request);
        }
        if (request.path == "/api/time/ntp" && request.method == HttpMethod::kPut) {
            return HandleTimeNtp(request);
        }
        if (request.path == "/api/time/system-time" &&
            request.method == HttpMethod::kPost) {
            return HandleTimeSystemTime(request);
        }
        if (request.path == "/api/time/sync" &&
            request.method == HttpMethod::kPost) {
            return HandleTimeSync(request);
        }
        if (request.path == "/api/network/interfaces" &&
            request.method == HttpMethod::kGet) {
            return HandleNetworkInterfaces(request);
        }
        if (StartsWith(request.path, "/api/network/interfaces/") &&
            (request.method == HttpMethod::kGet ||
             request.method == HttpMethod::kPut)) {
            return HandleNetworkInterface(request);
        }
        if (request.path == "/api/network/reload" &&
            request.method == HttpMethod::kPost) {
            return HandleNetworkReload(request);
        }
        if (request.path == "/api/upgrade/upload" &&
            request.method == HttpMethod::kPost) {
            return HandleUpgradeUpload(request);
        }
        if (request.path == "/api/upgrade/status" &&
            request.method == HttpMethod::kGet) {
            return HandleUpgradeStatus(request);
        }
        if (request.path == "/api/upgrade/validate" &&
            request.method == HttpMethod::kPost) {
            return HandleUpgradeValidate(request);
        }
        if (request.path == "/api/upgrade/start" &&
            request.method == HttpMethod::kPost) {
            return HandleUpgradeStart(request);
        }
        if (request.path == "/api/upgrade/cancel" &&
            request.method == HttpMethod::kPost) {
            return HandleUpgradeCancel(request);
        }
        if (request.path == "/api/upgrade/confirm-reboot" &&
            request.method == HttpMethod::kPost) {
            return HandleUpgradeConfirmReboot(request);
        }
        if (request.path == "/api/ai/status" &&
            request.method == HttpMethod::kGet) {
            return HandleAiStatus(request);
        }
        if (StartsWith(request.path, "/api/snapshot/") &&
            request.method == HttpMethod::kGet) {
            return HandleSnapshot(request);
        }
        if (StartsWith(request.path, "/api/hls/") &&
            request.method == HttpMethod::kGet) {
            return HandleHls(request);
        }
        if (StartsWith(request.path, "/api/webrtc") &&
            (request.method == HttpMethod::kPost ||
             request.method == HttpMethod::kDelete)) {
            return HandleWebrtc(request);
        }
        if (StartsWith(request.path, "/api/config/")) {
            return HandleConfig(request);
        }
        if (request.path == "/api/operations/export" &&
            request.method == HttpMethod::kGet) {
            return HandleOperationsExport(request);
        }
        if (request.path == "/api/operations" &&
            request.method == HttpMethod::kGet) {
            return HandleOperations(request);
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

    HttpServiceStats GetStats() const override {
        std::lock_guard<std::mutex> guard(mutex_);
        return stats_;
    }

    HttpListenAddress LocalAddress() const override {
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

private:
    struct PendingRequest {
        HttpRequest request;
        bool close_after_response = true;
    };

    struct HttpSession {
        std::string recv_buffer;
        std::string client_ip;
        std::deque<PendingRequest> pending_requests;
        uint64_t request_count = 0;
        uint64_t timeout_generation = 0;
        StreamFlvClientId flv_client_id = 0;
        std::shared_ptr<IStreamFlvSink> flv_sink;
        bool processing = false;
        bool closing = false;
        bool streaming = false;
    };

    class FlvConnectionSink : public IStreamFlvSink {
    public:
        FlvConnectionSink(HttpServiceImpl *owner, ConnectionId connection_id)
            : owner_(owner), connection_id_(connection_id) {}

        bool OnFlvChunk(const uint8_t *data, size_t size) override {
            return owner_ != nullptr &&
                   owner_->EnqueueStreamingChunk(connection_id_, data, size);
        }

    private:
        HttpServiceImpl *owner_ = nullptr;
        ConnectionId connection_id_ = 0;
    };

    static void HandleAccept(void *user, ConnectionId id, NetAddress peer) {
        HttpServiceImpl *self = static_cast<HttpServiceImpl *>(user);
        if (self != nullptr) {
            self->OnConnection(id, std::move(peer));
        }
    }

    static void HandleRead(void *user, ConnectionId id, const uint8_t *data,
                           size_t size) {
        HttpServiceImpl *self = static_cast<HttpServiceImpl *>(user);
        if (self != nullptr) {
            self->OnMessage(id, data, static_cast<uint32_t>(size));
        }
    }

    static void HandleClose(void *user, ConnectionId id) {
        HttpServiceImpl *self = static_cast<HttpServiceImpl *>(user);
        if (self != nullptr) {
            self->OnClose(id);
        }
    }

    static bool StartsWith(const std::string &value, const std::string &prefix) {
        return value.size() >= prefix.size() &&
               value.substr(0, prefix.size()) == prefix;
    }

    static bool IsConfigMutationRequest(const HttpRequest &request) {
        return request.method == HttpMethod::kPut &&
               StartsWith(request.path, "/api/config/");
    }

    infra::Executor *ExecutorForRequestLocked(const HttpRequest &request) const {
        if (IsConfigMutationRequest(request)) {
            return config_apply_executor_.get();
        }
        return task_executor_.get();
    }

    bool EnqueueStreamingChunk(ConnectionId connection_id, const uint8_t *data,
                               size_t size) {
        NetEngine *net_engine = nullptr;
        if (data == nullptr || size == 0) {
            return true;
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = sessions_.find(connection_id);
            if (iter == sessions_.end() || !iter->second.streaming) {
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
            INFRA_LOG_ERROR(kModuleName, "HTTP-FLV send failed conn=%llu size=%zu",
                            static_cast<unsigned long long>(connection_id),
                            size);
            (void)net_engine->Close(connection_id);
            return false;
        }
        return true;
    }

    bool TryHandleStreamingRequest(ConnectionId connection_id,
                                   const HttpRequest &request) {
        if (!StartsWith(request.path, "/api/flv/") ||
            request.method != HttpMethod::kGet) {
            return false;
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            ++stats_.total_requests;
        }
        INFRA_LOG_INFO(kModuleName, "HTTP-FLV request conn=%llu path=%s peer=%s",
                       static_cast<unsigned long long>(connection_id),
                       request.path.c_str(), request.client_ip.c_str());
        StartFlvStream(connection_id, request);
        return true;
    }

    void IncrementParseFailures() {
        std::lock_guard<std::mutex> guard(mutex_);
        ++stats_.parse_failures;
    }

    void IncrementNotFound() {
        std::lock_guard<std::mutex> guard(mutex_);
        ++stats_.not_found;
    }

    void IncrementAuthFailures() {
        std::lock_guard<std::mutex> guard(mutex_);
        ++stats_.auth_failures;
    }

    void IncrementPermissionDenied() {
        std::lock_guard<std::mutex> guard(mutex_);
        ++stats_.permission_denied;
    }

    live_stream::RequestContext MakeContext(const HttpRequest &request,
                                            const AuthPrincipal *principal) {
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

    uint64_t NextRequestId() {
        std::lock_guard<std::mutex> guard(mutex_);
        return ++next_request_id_;
    }

    AuthPrincipal Authenticate(const HttpRequest &request) {
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

    bool RequirePermission(const HttpRequest &request, AuthPermission permission,
                           const std::string &target, AuthPrincipal *principal) {
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

    void RecordOperation(const HttpRequest &request,
                         const AuthPrincipal &principal, OperationAction action,
                         const std::string &target, OperationResult result,
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
        record.module = kModuleName;
        record.action = action;
        record.target = target;
        record.result = result;
        record.reason = reason;
        (void)dependencies_.logger_service->RecordOperation(record);
    }

    // --- Auth handlers ---
#include "handlers/auth_handler.cpp.inc"

    // --- Media handlers ---
#include "handlers/media_handler.cpp.inc"

    // --- System handlers ---
#include "handlers/system_handler.cpp.inc"

    // --- Time handlers ---
#include "handlers/time_handler.cpp.inc"

    // --- Network handlers ---
#include "handlers/network_handler.cpp.inc"

    // --- Upgrade handlers ---
#include "handlers/upgrade_handler.cpp.inc"

    // --- AI handlers ---
#include "handlers/ai_handler.cpp.inc"

    // --- Snapshot handlers ---
#include "handlers/snapshot_handler.cpp.inc"

    // --- Stream handlers (HLS, WebRTC, FLV) ---
#include "handlers/stream_handler.cpp.inc"

    // --- Config handlers ---
#include "handlers/config_handler.cpp.inc"

    // --- Operations handlers ---
#include "handlers/operations_handler.cpp.inc"

    HttpResponse HandleStaticFile(const HttpRequest &request) {
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

    void OnConnection(ConnectionId connection_id, NetAddress peer) {
        std::string peer_ip = peer.ip;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            HttpSession session;
            session.client_ip = std::move(peer.ip);
            sessions_[connection_id] = session;
            ++stats_.active_connections;
        }
        INFRA_LOG_INFO(kModuleName, "HTTP accept conn=%llu peer=%s",
                       static_cast<unsigned long long>(connection_id),
                       peer_ip.c_str());
        ArmSessionTimer(connection_id, options_.request_timeout_ms);
    }

    void OnClose(ConnectionId connection_id) {
        StreamFlvClientId flv_client_id = 0;
        bool was_streaming = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = sessions_.find(connection_id);
            if (iter == sessions_.end()) {
                return;
            }
            flv_client_id = iter->second.flv_client_id;
            was_streaming = iter->second.streaming;
            sessions_.erase(iter);
        }
        if (flv_client_id != 0 && dependencies_.stream_hub_service != nullptr) {
            (void)dependencies_.stream_hub_service->DetachFlvClient(flv_client_id);
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (stats_.active_connections > 0) {
                --stats_.active_connections;
            }
        }
        INFRA_LOG_INFO(kModuleName, "HTTP close conn=%llu streaming=%d flv=%llu",
                       static_cast<unsigned long long>(connection_id),
                       was_streaming ? 1 : 0,
                       static_cast<unsigned long long>(flv_client_id));
    }

    void OnMessage(ConnectionId connection_id, const uint8_t *data,
                   uint32_t size) {
        if (data == nullptr) {
            return;
        }
        HttpResponse close_response;
        bool should_close = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = sessions_.find(connection_id);
            if (iter == sessions_.end() || iter->second.closing ||
                iter->second.streaming) {
                return;
            }
            ++iter->second.timeout_generation;
            iter->second.recv_buffer.append(reinterpret_cast<const char *>(data),
                                            size);
            should_close = !ParsePendingRequestsLocked(iter, &close_response);
        }

        if (should_close) {
            IncrementParseFailures();
            SendResponse(connection_id, close_response, true);
            return;
        }
        ArmSessionTimer(connection_id, options_.request_timeout_ms);
        TryPostNextRequest(connection_id);
    }

    void TryPostNextRequest(ConnectionId connection_id) {
        PendingRequest pending;
        infra::Executor *task_executor = nullptr;
        bool has_request = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = sessions_.find(connection_id);
            if (iter == sessions_.end() || iter->second.processing ||
                iter->second.pending_requests.empty()) {
                return;
            }
            pending = std::move(iter->second.pending_requests.front());
            iter->second.pending_requests.pop_front();
            iter->second.processing = true;
            has_request = true;
            task_executor = ExecutorForRequestLocked(pending.request);
        }
        if (!has_request) {
            return;
        }
        if (TryHandleStreamingRequest(connection_id, pending.request)) {
            return;
        }
        if (task_executor == nullptr ||
            task_executor->Post([this, connection_id,
                                 pending = std::move(pending)]() mutable {
                HttpResponse handled = HandleRequest(pending.request);
                SendResponse(connection_id, handled, pending.close_after_response);
            }) == false) {
            SendResponse(connection_id, StatusResponse(503, "Service Unavailable"),
                         true);
        }
    }

    void SendResponseAndClose(ConnectionId connection_id,
                              const HttpResponse &response) {
        SendResponse(connection_id, response, true);
    }

    void SendResponse(ConnectionId connection_id, const HttpResponse &response,
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

    void CompleteKeepAliveRequest(ConnectionId connection_id) {
        HttpResponse close_response;
        bool should_close = false;
        bool has_pending = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = sessions_.find(connection_id);
            if (iter == sessions_.end()) {
                return;
            }
            iter->second.processing = false;
            should_close = !ParsePendingRequestsLocked(iter, &close_response);
            has_pending = !iter->second.pending_requests.empty();
        }
        if (should_close) {
            IncrementParseFailures();
            SendResponse(connection_id, close_response, true);
            return;
        }
        if (has_pending) {
            TryPostNextRequest(connection_id);
            return;
        }
        ArmSessionTimer(connection_id, options_.connection_idle_timeout_ms);
    }

    bool
    ParsePendingRequestsLocked(std::map<ConnectionId, HttpSession>::iterator iter,
                               HttpResponse *close_response) {
        HttpSession &session = iter->second;
        const size_t max_buffer_size =
            static_cast<size_t>(options_.max_request_header_bytes) + 4 +
            options_.max_request_body_bytes;
        size_t parsed_count = 0;
        while (!session.closing &&
               parsed_count <
                   static_cast<size_t>(options_.max_pipelined_requests)) {
            if (session.recv_buffer.empty()) {
                return true;
            }
            if (session.recv_buffer.size() > max_buffer_size) {
                session.closing = true;
                if (close_response != nullptr) {
                    *close_response = StatusResponse(413, "Payload Too Large");
                }
                return false;
            }
            RawParseResult parsed = ParseRawRequest(
                session.recv_buffer, options_.max_request_header_bytes,
                options_.max_request_body_bytes, session.client_ip);
            if (parsed.status == RawParseStatus::kIncomplete) {
                return true;
            }
            if (parsed.status != RawParseStatus::kComplete ||
                parsed.consumed_bytes == 0 ||
                parsed.consumed_bytes > session.recv_buffer.size()) {
                session.closing = true;
                if (close_response != nullptr) {
                    *close_response = parsed.status == RawParseStatus::kPayloadTooLarge
                                          ? StatusResponse(413, "Payload Too Large")
                                          : StatusResponse(400, "Bad Request");
                }
                return false;
            }

            session.recv_buffer.erase(0, parsed.consumed_bytes);
            ++session.request_count;
            PendingRequest pending;
            pending.request = std::move(parsed.request);
            pending.close_after_response =
                !options_.enable_keep_alive || !parsed.keep_alive ||
                session.request_count >= options_.max_requests_per_connection;
            session.pending_requests.push_back(std::move(pending));
            const PendingRequest &queued = session.pending_requests.back();
            INFRA_LOG_INFO(kModuleName,
                           "HTTP request conn=%llu peer=%s %s %s query=%zu body=%zu",
                           static_cast<unsigned long long>(iter->first),
                           session.client_ip.c_str(),
                           HttpMethodName(queued.request.method),
                           queued.request.path.c_str(),
                           queued.request.query_string.size(),
                           queued.request.body.size());
            ++parsed_count;
            if (session.pending_requests.back().close_after_response) {
                session.closing = true;
                session.recv_buffer.clear();
                return true;
            }
        }
        return true;
    }

    void ArmSessionTimer(ConnectionId connection_id, uint32_t delay_ms) {
        NetEngine *net_engine = nullptr;
        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = sessions_.find(connection_id);
            if (iter == sessions_.end()) {
                return;
            }
            generation = ++iter->second.timeout_generation;
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
                    auto iter = sessions_.find(connection_id);
                    should_close = iter != sessions_.end() &&
                                   iter->second.timeout_generation == generation;
                    engine = dependencies_.net_engine;
                }
                if (should_close && engine != nullptr) {
                    (void)engine->Close(connection_id);
                }
            });
    }

    HttpServiceOptions options_;
    HttpServiceDependencies dependencies_;
    mutable std::mutex mutex_;
    std::unique_ptr<infra::Executor> task_executor_;
    std::unique_ptr<infra::Executor> config_apply_executor_;
    TcpServerId tcp_server_id_ = 0;
    std::map<ConnectionId, HttpSession> sessions_;
    HttpServiceStats stats_;
    uint64_t next_request_id_ = 0;
    bool initialized_ = false;
    bool started_ = false;
};

std::unique_ptr<IHttpService>
CreateHttpService(const HttpServiceOptions &options,
                  const HttpServiceDependencies &dependencies) {
    return std::unique_ptr<IHttpService>(
        new HttpServiceImpl(options, dependencies));
}

}  // namespace live_stream
