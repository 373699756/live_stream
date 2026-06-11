#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_

#include "http_access.h"
#include "http_router.h"
#include "media/media_streams.h"

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
class DeviceMedia;
class IAiView;
class IWebrtc;
class IHttp;

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
    DeviceMedia *device = nullptr;
    IAiView *ai = nullptr;
    IWebrtc *webrtc = nullptr;
    MediaStreams *media_streams = nullptr;
};

enum class HttpHandlerKind {
    kAuth = 0,
    kConfig,
    kOperations,
    kNetwork,
    kTime,
    kUpgrade,
    kSystem,
    kAlarm,
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
    DeviceMedia *device = nullptr;
    MediaStreams *media_streams = nullptr;
    IAlarm *alarm = nullptr;
    IRtsp *rtsp = nullptr;
    IWebrtc *webrtc = nullptr;
    IHttp *http = nullptr;
    IAiView *ai = nullptr;
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
std::unique_ptr<IHttpHandler> MakeAlarmHandler(
    HttpAccess *access, IAlarm *alarm);
std::unique_ptr<IHttpHandler> MakeMediaHandler(
    HttpAccess *access, IConfig *config,
    DeviceMedia *device, MediaStreams *media_streams,
    IRtsp *rtsp, IWebrtc *webrtc, IHttp *http);
std::unique_ptr<IHttpHandler> MakeAiHandler(
    HttpAccess *access, IConfig *config,
    IAiView *ai, DeviceMedia *device);
std::unique_ptr<IHttpHandler> MakeSnapshotHandler(
    HttpAccess *access, DeviceMedia *device);
std::unique_ptr<IHttpHandler> MakeEventStreamHandler(
    HttpAccess *access);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HANDLERS_HTTP_HANDLERS_H_
