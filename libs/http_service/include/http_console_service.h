#ifndef LIVE_STREAM_HTTP_CONSOLE_SERVICE_H_
#define LIVE_STREAM_HTTP_CONSOLE_SERVICE_H_

#include "http_service.h"
#include "media_source.h"

#include <memory>

namespace live_stream {

class IAuthService;
class IConfigService;
class ILoggerService;
class IAlarmService;
class INetworkService;
class IOnvifService;
class IRtspService;
class ISystemService;
class ITimeService;
class IUpgradeService;
class IWebrtcService;
class IMediaService;
class IAiView;
class ISnapshotView;
class NetEngine;

std::unique_ptr<IHttpService> CreateHttpConsoleService(
    const HttpServiceOptions &options, NetEngine *net_engine,
    IAuthService *auth_service, ILoggerService *logger_service,
    IConfigService *config_service, INetworkService *network_service,
    ITimeService *time_service, IAlarmService *alarm_service,
    IUpgradeService *upgrade_service, ISystemService *system_service,
    IRtspService *rtsp_service, IOnvifService *onvif_service,
    IAiView *ai_service, IMediaService *media_service,
    ISnapshotView *snapshot_service, IWebrtcService *webrtc_service,
    IMediaSource *media_source,
    IMediaFlvSource *media_flv_source,
    IMediaMjpegSource *media_mjpeg_source);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_CONSOLE_SERVICE_H_
