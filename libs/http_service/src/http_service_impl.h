#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVICE_IMPL_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVICE_IMPL_H_

#include "http_handler_context.h"
#include "http_connection_store.h"
#include "http_router.h"
#include "handlers/http_handlers.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace infra {
class Executor;
}  // namespace infra

namespace live_stream {

class HttpServiceImpl : public IHttpService, public HttpHandlerContext {
public:
    HttpServiceImpl(const HttpServiceOptions &options,
                    const HttpServiceDependencies &dependencies);
    ~HttpServiceImpl() override;

    bool Prepare();
    bool Start() override;
    void Stop() override;
    void Release();
    HttpResponse HandleRequest(const HttpRequest &request) override;
    HttpListenAddress LocalAddress() const override;
    HttpServiceStats GetStats() const override;

private:
    static void HandleAccept(void *user, ConnectionId id, NetAddress peer);
    static void HandleRead(void *user, ConnectionId id, const uint8_t *data,
                           size_t size);
    static void HandleClose(void *user, ConnectionId id);
    static bool IsConfigMutationRequest(const HttpRequest &request);
    static bool IsStreamRequest(const HttpRequest &request);
    static bool IsControlMutationRequest(const HttpRequest &request);
    static HttpResponse ParseFailureResponse(HttpConnectionParseFailure failure);
    static void LogRequests(
        const std::vector<HttpConnectionRequestLog> &request_logs);

    infra::Executor *ExecutorForRequestLocked(
        const HttpRequest &request) const;
    void DetachFlvClient(StreamFlvClientId client_id);
    void DetachFlvClients(const std::vector<StreamFlvClientId> &client_ids);
    bool TryHandleStreamingRequest(ConnectionId connection_id,
                                   const HttpRequest &request);
    void RegisterHandlers();

    AuthPrincipal Authenticate(const HttpRequest &request) override;
    bool RequirePermission(const HttpRequest &request,
                           AuthPermission permission,
                           const std::string &target,
                           AuthPrincipal *principal) override;
    live_stream::RequestContext MakeContext(
        const HttpRequest &request, const AuthPrincipal *principal) override;
    void RecordOperation(const HttpRequest &request,
                         const AuthPrincipal &principal,
                         OperationAction action,
                         const std::string &target,
                         OperationResult result,
                         const std::string &reason) override;
    void IncrementParseFailures() override;
    void IncrementAuthFailures() override;
    void IncrementPermissionDenied() override;
    void SendResponse(ConnectionId connection_id, const HttpResponse &response,
                      bool close_after_response) override;
    bool BeginFlvSession(
        ConnectionId connection_id,
        const std::shared_ptr<IStreamFlvSink> &sink) override;
    bool AttachFlvSessionClient(ConnectionId connection_id,
                                StreamFlvClientId client_id) override;
    bool EnqueueStreamingChunk(ConnectionId connection_id, const uint8_t *data,
                               size_t size) override;
    void CloseConnection(ConnectionId connection_id) override;

    void IncrementNotFound();
    uint64_t NextRequestId();
    HttpResponse HandleStaticFile(const HttpRequest &request);

    void OnConnection(ConnectionId connection_id, NetAddress peer);
    void OnClose(ConnectionId connection_id);
    void OnMessage(ConnectionId connection_id, const uint8_t *data,
                   uint32_t size);
    void TryPostNextRequest(ConnectionId connection_id);
    void SendResponseAndClose(ConnectionId connection_id,
                              const HttpResponse &response);
    void CompleteKeepAliveRequest(ConnectionId connection_id);
    HttpConnectionParseOptions MakeConnectionParseOptions() const;
    void ArmConnectionTimer(ConnectionId connection_id, uint32_t delay_ms);

    HttpServiceOptions options_;
    HttpServiceDependencies dependencies_;
    mutable std::mutex mutex_;
    std::unique_ptr<infra::Executor> task_executor_;
    std::unique_ptr<infra::Executor> stream_executor_;
    std::unique_ptr<infra::Executor> control_executor_;
    std::unique_ptr<infra::Executor> config_apply_executor_;
    HttpRouter router_;
    std::vector<std::unique_ptr<IHttpHandler>> handlers_;
    std::unique_ptr<IStreamingHttpHandler> streaming_handler_;
    TcpServerId tcp_server_id_ = 0;
    HttpConnectionStore connections_;
    HttpServiceStats stats_;
    uint64_t next_request_id_ = 0;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVICE_IMPL_H_
