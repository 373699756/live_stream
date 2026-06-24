#ifndef LIVE_STREAM_HTTP_SRC_HTTP_SESSION_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_SESSION_H_

#include "http_request_splitter.h"
#include "http_media_writer.h"
#include "net.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace live_stream {

struct PendingHttpRequest {
    // 同一 TCP 连接上的 pipeline 请求先排队，HttpServer 一次只派发一个，
    // 防止同一连接并发执行多个会修改状态的控制 API。
    HttpRequest request;
    bool close_after_response = true;
};

enum class HttpSessionParseFailure {
    kNone,
    kBadRequest,
    kPayloadTooLarge,
};

struct HttpSessionParseOptions {
    uint32_t max_request_header_bytes = 0;
    uint32_t max_request_body_bytes = 0;
    uint32_t max_pipelined_requests = 0;
    uint32_t max_requests_per_connection = 0;
    bool enable_keep_alive = false;
};

struct HttpRequestLog {
    ConnectionId connection_id = 0;
    std::string client_ip;
    HttpMethod method = HttpMethod::kGet;
    std::string path;
    size_t query_size = 0;
    size_t body_size = 0;
};

struct HttpSessionParseResult {
    bool success = true;
    bool has_pending = false;
    HttpSessionParseFailure failure = HttpSessionParseFailure::kNone;
};

struct ClosedHttpSessionInfo {
    HttpMediaClientHandle media_client;
    bool was_streaming = false;
};

struct HttpSessionStreamingInfo {
    ConnectionId connection_id = 0;
    std::string client_ip;
    HttpMediaClientType media_type = HttpMediaClientType::kNone;
    HttpMediaStreamState stream_state = HttpMediaStreamState::kNone;
    HttpMediaClientHandle media_client;
    StreamId stream_id = StreamId::kMain;
    bool streaming = false;
};

struct RenewedHttpSessionTimeout {
    // generation 是超时 timer 的版本号。请求推进、响应完成或进入 streaming 时会
    // 增加版本，旧 timer 回调必须被忽略。
    uint64_t generation = 0;
    event::TimerId replaced_timer_id = 0;
};

class HttpSession {
public:
    HttpSession(ConnectionId connection_id, std::string client_ip);

    ConnectionId connection_id() const;
    const std::string &client_ip() const;
    bool is_streaming() const;

    bool AppendRequestBytes(const uint8_t *data, uint32_t size);
    HttpSessionParseResult ParsePendingRequests(
        const HttpSessionParseOptions &options,
        std::vector<HttpRequestLog> *request_logs);
    bool TakeNextRequest(PendingHttpRequest *pending);
    HttpSessionParseResult CompleteKeepAliveRequest(
        const HttpSessionParseOptions &options,
        std::vector<HttpRequestLog> *request_logs);

    bool BeginStream(HttpMediaClientType type, StreamId stream_id);
    bool AttachStreamClient(HttpMediaClientHandle client);
    bool MarkStreamClosing();
    RenewedHttpSessionTimeout RenewTimeout();
    bool InstallTimeout(uint64_t generation, event::TimerId timer_id);
    event::TimerId CancelTimeout();
    bool ExpireTimeout(uint64_t generation);
    ClosedHttpSessionInfo Close();
    HttpMediaClientHandle TakeMediaClient();
    HttpSessionStreamingInfo StreamingInfo() const;

private:
    static HttpSessionParseFailure FailureFromSplitStatus(
        HttpRequestSplitStatus status);

    ConnectionId connection_id_ = 0;
    std::string client_ip_;
    HttpRequestSplitter splitter_;
    std::deque<PendingHttpRequest> pending_requests_;
    // requests_ 用于 max_requests_per_connection，达到上限后响应完即关闭。
    uint64_t requests_ = 0;
    uint64_t timeout_generation_ = 0;
    event::TimerId timer_id_ = 0;
    // media_client_ 只在 streaming 状态有效，保存 FLV/MJPEG/SSE 的外部 client id，
    // TCP close 时交给 HttpServer 统一 detach/unsubscribe。
    HttpMediaClientHandle media_client_;
    HttpMediaClientType media_type_ = HttpMediaClientType::kNone;
    HttpMediaStreamState stream_state_ = HttpMediaStreamState::kNone;
    StreamId stream_id_ = StreamId::kMain;
    // processing_ 保证同一 HTTP session 一次只执行一个 request。
    bool processing_ = false;
    // closing_ 表示当前连接不会再接收新的普通 HTTP 请求；可能是响应后关闭，
    // 也可能是已经切到 streaming。
    bool closing_ = false;
    bool streaming_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_SESSION_H_
