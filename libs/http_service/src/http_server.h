#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVER_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVER_H_

#include "http_connection_state_table.h"
#include "http_service.h"
#include "http_service_dependencies.h"
#include "http_stream_writer.h"

#include <cstddef>
#include <cstdint>
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
class HttpServer : public HttpStreamWriter {
public:
    HttpServer(const HttpServiceOptions &options,
               const HttpServiceDependencies &dependencies,
               HttpRequestHandler *request_handler);
    ~HttpServer() override;

    bool Prepare();
    bool Start();
    void Stop();
    void Release();

    HttpListenAddress LocalAddress() const;
    HttpServiceStats GetStats() const;

    void IncrementTotalRequests();
    void IncrementParseFailures();
    void IncrementNotFound();
    void IncrementAuthFailures();
    void IncrementPermissionDenied();

    void SendResponse(ConnectionId connection_id, const HttpResponse &response,
                      bool close_after_response) override;
    bool SendResponseSlices(ConnectionId connection_id,
                            const HttpResponse &response,
                            const HttpStreamSlice *body_slices,
                            size_t body_slice_count,
                            size_t body_size,
                            bool close_after_response) override;
    bool BeginStream(ConnectionId connection_id) override;
    bool AttachStreamClient(ConnectionId connection_id,
                            HttpStreamClientId client_id) override;
    bool EnqueueStreamingChunk(ConnectionId connection_id, const uint8_t *data,
                               size_t size) override;
    bool EnqueueStreamingSlices(ConnectionId connection_id,
                                const HttpStreamSlice *slices,
                                size_t slice_count) override;
    void SetCloseCallback(HttpStreamCloseCallback callback) override;
    void CloseConnection(ConnectionId connection_id) override;

private:
    static void HandleAccept(void *user, ConnectionId id, NetAddress peer);
    static void HandleRead(void *user, ConnectionId id, const uint8_t *data,
                           size_t size);
    static void HandleClose(void *user, ConnectionId id);
    static HttpResponse ParseFailureResponse(HttpConnectionParseFailure failure);
    static void LogRequests(
        const std::vector<HttpConnectionRequestLog> &request_logs);

    infra::Executor *ExecutorForRequestLocked(
        const HttpRequest &request) const;
    void NotifyStreamClosed(HttpStreamClientId client_id);
    void NotifyStreamsClosed(
        const std::vector<HttpStreamClientId> &client_ids);
    bool EnqueueStreamingSlices(ConnectionId connection_id,
                                const NetBufferSlices &slices,
                                size_t size);
    void OnConnection(ConnectionId connection_id, NetAddress peer);
    void OnClose(ConnectionId connection_id);
    void OnMessage(ConnectionId connection_id, const uint8_t *data,
                   uint32_t size);
    void TryPostNextRequest(ConnectionId connection_id);
    void CompleteKeepAliveRequest(ConnectionId connection_id);
    HttpConnectionParseOptions MakeConnectionParseOptions() const;
    void ArmConnectionTimer(ConnectionId connection_id, uint32_t delay_ms);

    HttpServiceOptions options_;
    HttpServiceDependencies dependencies_;
    HttpRequestHandler *request_handler_ = nullptr;
    HttpStreamCloseCallback close_callback_;
    mutable std::mutex mutex_;
    std::unique_ptr<infra::Executor> stream_executor_;
    std::unique_ptr<infra::Executor> control_executor_;
    TcpServerId tcp_server_id_ = 0;
    HttpConnectionStateTable connections_;
    HttpServiceStats stats_;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVER_H_
