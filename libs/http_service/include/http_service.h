#ifndef LIVE_STREAM_HTTP_SERVICE_H_
#define LIVE_STREAM_HTTP_SERVICE_H_

#include "infra/status.h"
#include "infra/service.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace live_stream {

class IAuthService;
class IConfigService;
class ILoggerService;
class IWebrtcService;
class MediaService;
class NetEngine;
class SnapshotService;

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

struct HttpServiceOptions {
    std::string listen_ip = "0.0.0.0";
    uint16_t listen_port = 80;
    uint32_t max_connections = 16;
    uint32_t max_request_header_bytes = 8 * 1024;
    uint32_t max_request_body_bytes = 64 * 1024;
    uint32_t send_queue_capacity = 64;
    uint32_t executor_queue_capacity = 128;
    uint32_t executor_worker_count = 1;
    uint32_t request_timeout_ms = 10000;
    uint32_t connection_idle_timeout_ms = 10000;
    uint32_t max_requests_per_connection = 1;
    uint32_t max_pipelined_requests = 1;
    std::string static_root;
    bool enable_static_files = true;
    bool enable_keep_alive = false;
};

struct HttpServiceDependencies {
    NetEngine* net_engine = nullptr;
    IAuthService* auth_service = nullptr;
    IConfigService* config_service = nullptr;
    ILoggerService* logger_service = nullptr;
    MediaService* media_service = nullptr;
    SnapshotService* snapshot_service = nullptr;
    IWebrtcService* webrtc_service = nullptr;
};

struct HttpServiceStats {
    uint64_t total_requests = 0;
    uint64_t auth_failures = 0;
    uint64_t permission_denied = 0;
    uint64_t parse_failures = 0;
    uint64_t not_found = 0;
    uint32_t active_connections = 0;
};

class IHttpService : public infra::IService {
 public:
    ~IHttpService() override = default;

    virtual infra::Result<HttpResponse> HandleRequest(
        const HttpRequest& request) = 0;
    virtual infra::Result<HttpListenAddress> LocalAddress() const = 0;
    virtual HttpServiceStats GetStats() const = 0;
};

std::unique_ptr<IHttpService> CreateHttpService(
    const HttpServiceOptions& options,
    const HttpServiceDependencies& dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_H_
