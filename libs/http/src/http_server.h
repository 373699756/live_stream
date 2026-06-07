#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVER_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVER_H_

#include "http.h"
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

namespace infra {
class Executor;
}  // namespace infra

namespace live_stream {

class HttpRequestHandler {
public:
    virtual ~HttpRequestHandler() = default;

    virtual bool ShouldUseStreamExecutor(const HttpRequest &request) const = 0;
    virtual HttpResponse HandleHttpRequest(const HttpRequest &request) = 0;
    virtual bool HandleStreamingHttpRequest(ConnectionId connection_id,
                                            const HttpRequest &request) = 0;
};

// Private HTTP server core. It owns TCP, HTTP/1.1 connection parsing,
// executor selection, keep-alive, response sending, and streaming output.
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

    void IncrementTotalRequests();
    void IncrementParseFailures();
    void IncrementNotFound();
    void IncrementAuthFailures();
    void IncrementPermissionDenied();

    void SendResponse(ConnectionId connection_id, const HttpResponse &response,
                      bool close_after_response) override;
    bool SendResponseSlices(ConnectionId connection_id,
                            const HttpResponse &response,
                            const HttpMediaSlice *body_slices,
                            size_t body_slice_count,
                            size_t body_size,
                            bool close_after_response) override;
    bool BeginStream(ConnectionId connection_id) override;
    bool AttachStreamClient(ConnectionId connection_id,
                            HttpMediaClientHandle client) override;
    bool EnqueueStreamingChunk(ConnectionId connection_id, const uint8_t *data,
                               size_t size) override;
    bool EnqueueStreamingSlices(ConnectionId connection_id,
                                const HttpMediaSlice *slices,
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

    infra::Executor *ExecutorForRequestLocked(
        const HttpRequest &request) const;
    void NotifyStreamClosed(const HttpMediaClientHandle &client);
    void NotifyStreamsClosed(
        const std::vector<HttpMediaClientHandle> &clients);
    bool EnqueueStreamingSlices(ConnectionId connection_id,
                                const NetBufferSlices &slices,
                                size_t size);
    void OnConnection(ConnectionId connection_id, NetAddress peer);
    void OnClose(ConnectionId connection_id, TcpCloseReason reason);
    void OnMessage(ConnectionId connection_id, const uint8_t *data,
                   uint32_t size);
    void TryPostNextRequest(ConnectionId connection_id);
    void CompleteKeepAliveRequest(ConnectionId connection_id);
    HttpSessionParseOptions MakeConnectionParseOptions() const;
    void ArmConnectionTimer(ConnectionId connection_id, uint32_t delay_ms);

    HttpOptions options_;
    NetEngine *net_engine_ = nullptr;
    HttpRequestHandler *request_handler_ = nullptr;
    HttpMediaCloseCallback close_callback_;
    mutable std::mutex mutex_;
    std::unique_ptr<infra::Executor> stream_executor_;
    std::unique_ptr<infra::Executor> control_executor_;
    TcpServerId tcp_server_id_ = 0;
    std::map<ConnectionId, std::unique_ptr<HttpSession>> sessions_;
    HttpStats stats_;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVER_H_
