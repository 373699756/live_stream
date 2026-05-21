#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_

#include "http_handler_context.h"
#include "http_router.h"

#include <memory>

namespace live_stream {

class IAuthService;
class IConfigService;
class ILoggerService;
class IAlarmService;
class INetworkService;
class ITimeService;
class IUpgradeService;
class ISystemService;
class IRtspService;
class IOnvifService;
class IMediaService;
class IAiView;
class ISnapshotView;
class IWebrtcService;
class IStreamHubService;

struct AuthHandlerDependencies {
    IAuthService *auth_service = nullptr;
};

struct ConfigHandlerDependencies {
    IConfigService *config_service = nullptr;
};

struct OperationsHandlerDependencies {
    ILoggerService *logger_service = nullptr;
};

struct DeviceHandlerDependencies {
    INetworkService *network_service = nullptr;
    ITimeService *time_service = nullptr;
    IUpgradeService *upgrade_service = nullptr;
};

struct SystemHandlerDependencies {
    ILoggerService *logger_service = nullptr;
    IConfigService *config_service = nullptr;
    IAuthService *auth_service = nullptr;
    ISystemService *system_service = nullptr;
    ITimeService *time_service = nullptr;
    INetworkService *network_service = nullptr;
    IAlarmService *alarm_service = nullptr;
    IUpgradeService *upgrade_service = nullptr;
    IRtspService *rtsp_service = nullptr;
    IOnvifService *onvif_service = nullptr;
    IMediaService *media_service = nullptr;
    IAiView *ai_service = nullptr;
    ISnapshotView *snapshot_service = nullptr;
    IWebrtcService *webrtc_service = nullptr;
    IStreamHubService *stream_hub_service = nullptr;
};

struct MediaHandlerDependencies {
    IConfigService *config_service = nullptr;
    IMediaService *media_service = nullptr;
    IAiView *ai_service = nullptr;
    ISnapshotView *snapshot_service = nullptr;
    IWebrtcService *webrtc_service = nullptr;
    IStreamHubService *stream_hub_service = nullptr;
};

std::unique_ptr<IHttpHandler> CreateAuthHttpHandler(
    HttpHandlerContext *context, const AuthHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> CreateConfigHttpHandler(
    HttpHandlerContext *context, const ConfigHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> CreateOperationsHttpHandler(
    HttpHandlerContext *context,
    const OperationsHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> CreateNetworkHttpHandler(
    HttpHandlerContext *context, const DeviceHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> CreateTimeHttpHandler(
    HttpHandlerContext *context, const DeviceHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> CreateUpgradeHttpHandler(
    HttpHandlerContext *context, const DeviceHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> CreateSystemHttpHandler(
    HttpHandlerContext *context, const SystemHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> CreateMediaHttpHandler(
    HttpHandlerContext *context, const MediaHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> CreateAiHttpHandler(
    HttpHandlerContext *context, const MediaHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> CreateSnapshotHttpHandler(
    HttpHandlerContext *context, const MediaHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> CreateHlsHttpHandler(
    HttpHandlerContext *context, const MediaHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> CreateWebrtcHttpHandler(
    HttpHandlerContext *context, const MediaHandlerDependencies &dependencies);

class IStreamingHttpHandler {
public:
    virtual ~IStreamingHttpHandler() = default;

    virtual bool CanHandleStreamingRequest(const HttpRequest &request) const = 0;
    virtual void HandleStreamingRequest(ConnectionId connection_id,
                                        const HttpRequest &request) = 0;
};

std::unique_ptr<IStreamingHttpHandler> CreateStreamingHttpHandler(
    HttpHandlerContext *context, const MediaHandlerDependencies &dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_
