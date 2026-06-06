#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_

#include "http_access.h"
#include "http_router.h"
#include "http_stream_writer.h"
#include "media_source.h"

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

struct SystemStatusSources {
    ILoggerService *logger_service = nullptr;
    IConfigService *config_service = nullptr;
    IAuthService *auth_service = nullptr;
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
    IMediaSource *media_source = nullptr;
};

std::unique_ptr<IHttpHandler> CreateAuthHttpHandler(
    HttpAccess *access, IAuthService *auth_service);
std::unique_ptr<IHttpHandler> CreateConfigHttpHandler(
    HttpAccess *access, IConfigService *config_service);
std::unique_ptr<IHttpHandler> CreateOperationsHttpHandler(
    HttpAccess *access, ILoggerService *logger_service);
std::unique_ptr<IHttpHandler> CreateNetworkHttpHandler(
    HttpAccess *access, INetworkService *network_service);
std::unique_ptr<IHttpHandler> CreateTimeHttpHandler(
    HttpAccess *access, ITimeService *time_service);
std::unique_ptr<IHttpHandler> CreateUpgradeHttpHandler(
    HttpAccess *access, IUpgradeService *upgrade_service);
std::unique_ptr<IHttpHandler> CreateSystemHttpHandler(
    HttpAccess *access, ISystemService *system_service,
    const SystemStatusSources &status_sources);
std::unique_ptr<IHttpHandler> CreateMediaHttpHandler(
    HttpAccess *access, IConfigService *config_service,
    IMediaService *media_service, IMediaSource *media_source,
    IWebrtcService *webrtc_service);
std::unique_ptr<IHttpHandler> CreateAiHttpHandler(
    HttpAccess *access, IConfigService *config_service,
    IAiView *ai_service);
std::unique_ptr<IHttpHandler> CreateSnapshotHttpHandler(
    HttpAccess *access, IMediaService *media_service,
    ISnapshotView *snapshot_service);
std::unique_ptr<IHttpHandler> CreateHlsHttpHandler(
    HttpAccess *access, IMediaService *media_service,
    IMediaSource *media_source);
std::unique_ptr<IHttpHandler> CreateWebrtcHttpHandler(
    HttpAccess *access, IMediaService *media_service,
    IWebrtcService *webrtc_service);
std::unique_ptr<IHttpHandler> CreateEventStreamHttpHandler(
    HttpAccess *access);

class IStreamingHttpHandler {
public:
    virtual ~IStreamingHttpHandler() = default;

    virtual bool CanHandleStreamingRequest(const HttpRequest &request) const = 0;
    virtual void HandleStreamingRequest(ConnectionId connection_id,
                                        const HttpRequest &request) = 0;
};

std::unique_ptr<IStreamingHttpHandler> CreateStreamingHttpHandler(
    HttpAccess *access, HttpStreamWriter *writer, IMediaService *media_service,
    IMediaSource *media_source,
    IMediaFlvSource *media_flv_source,
    IMediaMjpegSource *media_mjpeg_source);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_
