#ifndef LIVE_STREAM_HTTP_SRC_HTTP_SERVER_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_SERVER_H_

#include "http.h"
#include "http_response_sender.h"
#include "http_dependencies.h"
#include "http_media_writer.h"
#include "http_session.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace live_stream {

class HttpRequestHandler {
public:
    virtual ~HttpRequestHandler() = default;

    virtual bool ShouldUseStreamExecutor(const HttpRequest &request) const = 0;
    virtual HttpResponse HandleHttpRequest(const HttpRequest &request) = 0;
    virtual HttpStreamingRequestResult HandleStreamingHttpRequest(
        ConnectionId connection_id, const HttpRequest &request) = 0;
};

// HTTP server 内核只负责连接和 HTTP/1.1 生命周期。业务路由在 HttpImpl，
// HLS/FLV/MJPEG/SSE 等长连接通过 HttpMediaWriter 回调进来。
class HttpServer : public HttpMediaWriter {
public:
    HttpServer(const HttpOptions &options,
               const HttpDependencies &dependencies,
               HttpRequestHandler *request_handler);
    ~HttpServer() override;

    bool Prepare();
    bool Start();
    void Stop();
    void Release();

    HttpListenAddress LocalAddress() const;
    HttpStats GetStats() const;
    std::vector<HttpStreamSessionInfo>
    ListStreamSessionInfo() const;

    void IncrementTotalRequests();
    void IncrementParseFailures();
    void IncrementNotFound();
    void IncrementAuthFailures();
    void IncrementPermissionDenied();

    void SendResponse(ConnectionId connection_id, const HttpResponse &response,
                      bool close_after_response) override;
    bool BeginStream(ConnectionId connection_id,
                     HttpMediaClientType type,
                     StreamId stream_id) override;
    bool AttachStreamClient(ConnectionId connection_id,
                            HttpMediaClientHandle client) override;
    bool EnqueueStreamingChunk(ConnectionId connection_id, const uint8_t *data,
                               size_t size) override;
    bool EnqueueStreamingSlices(ConnectionId connection_id,
                                const MediaOutSlice *slices,
                                size_t slice_count) override;
    void SetCloseCallback(HttpMediaCloseCallback callback) override;
    void CloseConnection(ConnectionId connection_id) override;

private:
    static void HandleAccept(void *user, ConnectionId id, NetAddress peer);
    static void HandleRead(void *user, ConnectionId id, const uint8_t *data,
                           size_t size);
    static void HandleClose(void *user, ConnectionId id,
                            TcpCloseReason reason);
    static HttpResponse ParseFailureResponse(HttpSessionParseFailure failure);
    static void LogRequests(const std::vector<HttpRequestLog> &request_logs);
    static HttpStreamSessionInfo BuildStreamSessionInfo(
        const HttpSessionStreamingInfo &session,
        const NetConnectionInfo &connection);

    event::Executor *ExecutorForRequestLocked(
        const HttpRequest &request) const;
    void NotifyStreamClosed(const HttpMediaClientHandle &client);
    void NotifyStreamsClosed(
        const std::vector<HttpMediaClientHandle> &clients);
    void CloseConnectionWithReason(ConnectionId connection_id,
                                   TcpCloseReason reason);
    bool MarkStreamingClosing(ConnectionId connection_id);
    bool HandleStreamingRequestResult(ConnectionId connection_id,
                                      const HttpRequest &request,
                                      HttpStreamingRequestResult result);
    void OnConnection(ConnectionId connection_id, NetAddress peer);
    void OnClose(ConnectionId connection_id, TcpCloseReason reason);
    void OnMessage(ConnectionId connection_id, const uint8_t *data,
                   uint32_t size);
    void TryPostNextRequest(ConnectionId connection_id);
    void CompleteKeepAliveRequest(ConnectionId connection_id);
    HttpSessionParseOptions MakeConnectionParseOptions() const;
    void ArmConnectionTimer(ConnectionId connection_id, uint32_t delay_ms);
    static void CancelNetTimer(event::Loop *loop, event::TimerId timer_id);

    HttpOptions options_;
    HttpResponseSender response_sender_;
    INetIo *net_io_ = nullptr;
    event::Loop *net_loop_ = nullptr;
    // request_handler_ 非 owning，由 HttpImpl 持有；HttpServer 停止前不会释放它。
    HttpRequestHandler *request_handler_ = nullptr;
    // close_callback_ 由 http_media 注册，必须在锁外调用，避免 HTTP 锁和媒体锁
    // 互相等待。
    HttpMediaCloseCallback close_callback_;
    mutable std::mutex mutex_;
    // stream_executor_ 处理 /live、SSE、WHEP 等可能长时间占用的请求；
    // control_executor_ 处理登录、配置、状态等短请求。
    std::unique_ptr<event::Executor> stream_executor_;
    std::unique_ptr<event::Executor> control_executor_;
    TcpServerId tcp_server_id_ = 0;
    // sessions_ 是 HTTP 层唯一的连接状态表。进入 streaming 后仍保留 session，
    // 但只用于断连回收媒体 client，不再解析 HTTP 请求。
    std::map<ConnectionId, std::unique_ptr<HttpSession>> sessions_;
    HttpStats stats_;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_SERVER_H_
