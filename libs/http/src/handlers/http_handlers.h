#ifndef LIVE_STREAM_HTTP_SRC_HANDLERS_HTTP_HANDLERS_H_
#define LIVE_STREAM_HTTP_SRC_HANDLERS_HTTP_HANDLERS_H_

#include "http_access.h"
#include "http_router.h"
#include "media/media_streams.h"

#include <memory>

namespace live_stream {

class IAuth;
class IConfig;
class ILogger;
class IAlarm;
class INetwork;
class ITime;
class IUpgrade;
class ISystem;
class IRtsp;
class OnvifServer;
class DeviceMedia;
class IAiView;
class IWebrtc;
class IHttp;

struct SystemOverviewSources {
    ILogger *logger = nullptr;
    IConfig *config = nullptr;
    IAuth *auth = nullptr;
    ITime *time = nullptr;
    INetwork *network = nullptr;
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
    INetwork *network = nullptr;
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
    SystemOverviewSources system_overview_sources;
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
    HttpAccess *access, INetwork *network);
std::unique_ptr<IHttpHandler> MakeTimeHandler(
    HttpAccess *access, ITime *time);
std::unique_ptr<IHttpHandler> MakeUpgradeHandler(
    HttpAccess *access, IUpgrade *upgrade);
std::unique_ptr<IHttpHandler> MakeSystemHandler(
    HttpAccess *access, ISystem *system,
    const SystemOverviewSources &overview_sources);
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

#endif  // LIVE_STREAM_HTTP_SRC_HANDLERS_HTTP_HANDLERS_H_
