#ifndef LIVE_STREAM_HTTP_HTTP_H_
#define LIVE_STREAM_HTTP_HTTP_H_

#include "media/stream_types.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

struct HttpDependencies;

enum class HttpMethod {
    kGet,
    kPost,
    kPut,
    kDelete,
};

struct HttpRequest {
    HttpMethod method = HttpMethod::kGet;
    std::string path;
    std::string query_string;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string client_ip;
    std::string request_id;
};

struct HttpResponse {
    int status_code = 200;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpListenAddress {
    std::string ip;
    uint16_t port = 0;
};

struct HttpOptions {
    std::string listen_ip = "0.0.0.0";
    uint16_t listen_port = 80;
    uint32_t max_connections = 16;
    uint32_t max_request_header_bytes = 8 * 1024;
    uint32_t max_request_body_bytes = 64 * 1024;
    uint32_t send_queue_capacity = 64;
    uint32_t send_buffer_limit_bytes = 8 * 1024 * 1024;
    uint32_t stream_executor_queue_capacity = 128;
    uint32_t stream_executor_worker_count = 2;
    uint32_t control_executor_queue_capacity = 16;
    uint32_t control_executor_worker_count = 1;
    uint32_t request_timeout_ms = 10000;
    uint32_t connection_idle_timeout_ms = 10000;
    uint32_t max_requests_per_connection = 1;
    uint32_t max_pipelined_requests = 1;
    std::string static_root;
    bool enable_static_files = true;
    bool enable_keep_alive = false;
};

struct HttpStats {
    uint64_t total_requests = 0;
    uint64_t auth_failures = 0;
    uint64_t permission_denied = 0;
    uint64_t parse_failures = 0;
    uint64_t not_found = 0;
    uint32_t active_connections = 0;
};

struct HttpStreamingSessionDiagnostics {
    uint64_t connection_id = 0;
    std::string protocol;
    std::string session_id;
    std::string client_id;
    std::string stream_state;
    StreamId stream_id = StreamId::kMain;
    std::string client_ip;
    std::string remote_address;
    std::string local_address;
    uint32_t pending_bytes = 0;
    uint32_t send_queue_length = 0;
    int64_t last_write_at_ms = 0;
    std::string close_reason;
    bool open = false;
};

// IHttpHandler is the extension point for domain-specific route handlers.
// Implementations register routes with the router in RegisterRoutes().
// This interface enables handlers/ subdirectory decomposition (Phase 1 goal).
class IHttpRouter;

class IHttpHandler {
public:
    virtual ~IHttpHandler() = default;
    // Called once during IHttp startup to register routes.
    virtual void RegisterRoutes(IHttpRouter *router) = 0;
};

class IHttp {
public:
    virtual ~IHttp() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual HttpResponse HandleRequest(const HttpRequest &request) = 0;
    virtual HttpListenAddress LocalAddress() const = 0;
    virtual HttpStats GetStats() const = 0;
    virtual std::vector<HttpStreamingSessionDiagnostics>
    GetStreamingSessionDiagnostics() const = 0;
};

std::unique_ptr<IHttp> CreateHttp(
    const HttpOptions &options,
    const HttpDependencies &dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_HTTP_H_
