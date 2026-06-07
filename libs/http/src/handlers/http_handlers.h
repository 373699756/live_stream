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

enum class HttpHandlerKind {
    kAuth = 0,
    kConfig,
    kOperations,
    kNetwork,
    kTime,
    kUpgrade,
    kSystem,
    kMedia,
    kAi,
    kSnapshot,
    kEventStream,
};

struct HttpHandlerDependencies {
    HttpAccess *access = nullptr;
    IAuth *auth = nullptr;
    IConfig *config = nullptr;
    ILogger *logger = nullptr;
    INetworkConfig *network_config = nullptr;
    ITime *time = nullptr;
    IUpgrade *upgrade = nullptr;
    ISystem *system = nullptr;
    IDeviceMedia *device_media = nullptr;
    IMediaSource *media_source = nullptr;
    IRtsp *rtsp = nullptr;
    IWebrtc *webrtc = nullptr;
    IAiView *ai = nullptr;
    ISnapshotView *snapshot = nullptr;
    SystemStatusSources system_status_sources;
};

std::unique_ptr<IHttpHandler> CreateHttpHandler(
    HttpHandlerKind kind,
    const HttpHandlerDependencies &dependencies);

std::unique_ptr<IHttpHandler> MakeAuthHandler(
    HttpAccess *access, IAuth *auth);
std::unique_ptr<IHttpHandler> MakeConfigHandler(
    HttpAccess *access, IConfig *config);
std::unique_ptr<IHttpHandler> MakeOperationsHandler(
    HttpAccess *access, ILogger *logger);
std::unique_ptr<IHttpHandler> MakeNetworkHandler(
    HttpAccess *access, INetworkConfig *network_config);
std::unique_ptr<IHttpHandler> MakeTimeHandler(
    HttpAccess *access, ITime *time);
std::unique_ptr<IHttpHandler> MakeUpgradeHandler(
    HttpAccess *access, IUpgrade *upgrade);
std::unique_ptr<IHttpHandler> MakeSystemHandler(
    HttpAccess *access, ISystem *system,
    const SystemStatusSources &status_sources);
std::unique_ptr<IHttpHandler> MakeMediaHandler(
    HttpAccess *access, IConfig *config,
    IDeviceMedia *device_media, IMediaSource *media_source,
    IRtsp *rtsp, IWebrtc *webrtc);
std::unique_ptr<IHttpHandler> MakeAiHandler(
    HttpAccess *access, IConfig *config,
    IAiView *ai, IDeviceMedia *device_media);
std::unique_ptr<IHttpHandler> MakeSnapshotHandler(
    HttpAccess *access, IDeviceMedia *device_media,
    ISnapshotView *snapshot);
std::unique_ptr<IHttpHandler> MakeEventStreamHandler(
    HttpAccess *access);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_
