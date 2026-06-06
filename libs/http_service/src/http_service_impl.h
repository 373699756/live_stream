#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVICE_IMPL_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVICE_IMPL_H_

#include "http_access.h"
#include "http_router.h"
#include "http_server.h"
#include "http_service_dependencies.h"
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

class HttpServiceImpl : public IHttpService,
                        public HttpAccess,
                        public HttpRequestHandler {
public:
    HttpServiceImpl(const HttpServiceOptions &options,
                    const HttpServiceDependencies &dependencies);
    ~HttpServiceImpl() override;

    bool Prepare();
    bool Start() override;
    void Stop() override;
    void Release();
    HttpResponse HandleRequest(const HttpRequest &request) override;
    bool ShouldUseStreamExecutor(const HttpRequest &request) const override;
    HttpResponse HandleHttpRequest(const HttpRequest &request) override;
    bool HandleStreamingHttpRequest(ConnectionId connection_id,
                                    const HttpRequest &request) override;
    HttpListenAddress LocalAddress() const override;
    HttpServiceStats GetStats() const override;

    void ConfigureConsoleHandlers(
        IAuthService *auth_service, ILoggerService *logger_service,
        IConfigService *config_service, INetworkService *network_service,
        ITimeService *time_service, IAlarmService *alarm_service,
        IUpgradeService *upgrade_service, ISystemService *system_service,
        IRtspService *rtsp_service, OnvifServer *onvif_service,
        IAiView *ai_service, IMediaService *media_service,
        ISnapshotView *snapshot_service, IWebrtcService *webrtc_service,
        IMediaSource *media_source,
        IMediaFlvSource *media_flv_source,
        IMediaMjpegSource *media_mjpeg_source);

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

    void StopInternal();
    void ReleaseInternal();
    void IncrementNotFound();
    uint64_t NextRequestId();
    HttpResponse HandleStaticFile(const HttpRequest &request);

    HttpServiceOptions options_;
    HttpServiceDependencies dependencies_;
    IAuthService *auth_service_ = nullptr;
    ILoggerService *logger_service_ = nullptr;
    mutable std::mutex mutex_;
    std::unique_ptr<HttpServer> server_;
    HttpRouter router_;
    std::vector<std::unique_ptr<IHttpHandler>> handlers_;
    std::unique_ptr<IStreamingHttpHandler> streaming_handler_;
    uint64_t next_request_id_ = 0;
    bool initialized_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SERVICE_IMPL_H_
