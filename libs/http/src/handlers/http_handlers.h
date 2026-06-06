#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_

#include "http_access.h"
#include "http_router.h"
#include "media_source.h"

#include <memory>

namespace live_stream {

class IAuth;
class IConfig;
class ILogger;
class IAlarm;
class INetworkConfig;
class ITime;
class IUpgrade;
class ISystem;
class IRtsp;
class OnvifServer;
class IDeviceMedia;
class IAiView;
class ISnapshotView;
class IWebrtc;

struct SystemStatusSources {
    ILogger *logger = nullptr;
    IConfig *config = nullptr;
    IAuth *auth = nullptr;
    ITime *time = nullptr;
    INetworkConfig *network_config = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
    IRtsp *rtsp = nullptr;
    OnvifServer *onvif = nullptr;
    IDeviceMedia *device_media = nullptr;
    IAiView *ai = nullptr;
    ISnapshotView *snapshot = nullptr;
    IWebrtc *webrtc = nullptr;
    IMediaSource *media_source = nullptr;
};

std::unique_ptr<IHttpHandler> CreateAuthHttpHandler(
    HttpAccess *access, IAuth *auth);
std::unique_ptr<IHttpHandler> CreateConfigHttpHandler(
    HttpAccess *access, IConfig *config);
std::unique_ptr<IHttpHandler> CreateOperationsHttpHandler(
    HttpAccess *access, ILogger *logger);
std::unique_ptr<IHttpHandler> CreateNetworkHttpHandler(
    HttpAccess *access, INetworkConfig *network_config);
std::unique_ptr<IHttpHandler> CreateTimeHttpHandler(
    HttpAccess *access, ITime *time);
std::unique_ptr<IHttpHandler> CreateUpgradeHttpHandler(
    HttpAccess *access, IUpgrade *upgrade);
std::unique_ptr<IHttpHandler> CreateSystemHttpHandler(
    HttpAccess *access, ISystem *system,
    const SystemStatusSources &status_sources);
std::unique_ptr<IHttpHandler> CreateMediaHttpHandler(
    HttpAccess *access, IConfig *config,
    IDeviceMedia *device_media, IMediaSource *media_source,
    IWebrtc *webrtc);
std::unique_ptr<IHttpHandler> CreateAiHttpHandler(
    HttpAccess *access, IConfig *config,
    IAiView *ai);
std::unique_ptr<IHttpHandler> CreateSnapshotHttpHandler(
    HttpAccess *access, IDeviceMedia *device_media,
    ISnapshotView *snapshot);
std::unique_ptr<IHttpHandler> CreateEventStreamHttpHandler(
    HttpAccess *access);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_
