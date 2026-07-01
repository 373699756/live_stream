#ifndef LIVE_STREAM_HTTP_SRC_HTTP_IMPL_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_IMPL_H_

#include "http_access.h"
#include "http_router.h"
#include "http_server.h"
#include "http_media.h"
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

namespace event {
class Dispatcher;
}  // namespace event

class IRtspSessionReader;
class IWebrtcReader;

struct HttpControlRefs {
    IAuth *auth = nullptr;
    ILogger *logger = nullptr;
    IConfig *config = nullptr;
    INetwork *network = nullptr;
    ITime *time = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
    ISystem *system = nullptr;
    IAiReader *ai = nullptr;
    DeviceMedia *device = nullptr;
};

struct HttpMediaRefs {
    IConfig *config = nullptr;
    DeviceMedia *device = nullptr;
    MediaStreams *media_streams = nullptr;
    IRtspSessionReader *rtsp_session_reader = nullptr;
    IWebrtcReader *webrtc_reader = nullptr;
    IWebrtc *webrtc = nullptr;
};

struct HttpStreamingRefs {
    DeviceMedia *device = nullptr;
    MediaStreams *media_streams = nullptr;
    event::Dispatcher *event = nullptr;
};

class HttpImpl : public IHttp,
                 public HttpAccess,
                 public HttpRequestHandler {
public:
    HttpImpl(const HttpOptions &options,
             event::Loop *net_loop,
             INetwork *network,
             ITime *time,
             IAlarm *alarm,
             IUpgrade *upgrade,
             ISystem *system,
             IAiReader *ai,
             DeviceMedia *device,
             IWebrtc *webrtc);
    ~HttpImpl() override;

    bool Prepare();
    bool Start() override;
    void Stop() override;
    void Release();
    HttpResponse HandleRequest(const HttpRequest &request) override;
    bool ShouldUseStreamExecutor(const HttpRequest &request) const override;
    HttpResponse HandleHttpRequest(const HttpRequest &request) override;
    HttpStreamingRequestResult HandleStreamingHttpRequest(
        ConnectionId connection_id, const HttpRequest &request) override;
    HttpListenAddress LocalAddress() const override;
    HttpStats GetStats() const override;
    std::vector<HttpStreamSessionInfo>
    ListStreamSessionInfo() const override;

private:
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

    void InitializeHandlers(INetwork *network,
                            ITime *time,
                            IAlarm *alarm,
                            IUpgrade *upgrade,
                            ISystem *system,
                            IAiReader *ai,
                            DeviceMedia *device,
                            IWebrtc *webrtc);
    void ConfigureCloseCallback(MediaStreams *media_streams);
    void InitializeControlHandlers(const HttpControlRefs &refs);
    void InitializeMediaHandlers(const HttpMediaRefs &refs);
    void InitializeStreamingHandler(const HttpStreamingRefs &refs);
    void RegisterRoutes();
    void StopInternal();
    void ReleaseInternal();
    void IncrementNotFound();
    uint64_t NextRequestId();
    HttpResponse HandleStaticFile(const HttpRequest &request);

    HttpOptions options_;
    IAuth *auth_ = nullptr;
    ILogger *logger_ = nullptr;
    mutable std::mutex mutex_;
    std::unique_ptr<HttpServer> server_;
    HttpRouter router_;
    std::vector<std::unique_ptr<IHttpHandler>> handlers_;
    std::unique_ptr<IStreamingHttpHandler> streaming_handler_;
    uint64_t next_request_id_ = 0;
    bool initialized_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_IMPL_H_
