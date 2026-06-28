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
class IRtspSessionReader;
class IOnvifStatusReader;
class DeviceMedia;
class IAiReader;
class IWebrtc;
class IWebrtcStatusReader;
class IHttp;

struct AuthHandlerDependencies {
    HttpAccess *access = nullptr;
    IAuth *auth = nullptr;
};

struct ConfigHandlerDependencies {
    HttpAccess *access = nullptr;
    IConfig *config = nullptr;
};

struct OperationsHandlerDependencies {
    HttpAccess *access = nullptr;
    ILogger *logger = nullptr;
};

struct NetworkHandlerDependencies {
    HttpAccess *access = nullptr;
    INetwork *network = nullptr;
};

struct TimeHandlerDependencies {
    HttpAccess *access = nullptr;
    ITime *time = nullptr;
};

struct UpgradeHandlerDependencies {
    HttpAccess *access = nullptr;
    IUpgrade *upgrade = nullptr;
};

struct AlarmHandlerDependencies {
    HttpAccess *access = nullptr;
    IAlarm *alarm = nullptr;
};

struct SystemOverviewSources {
    ILogger *logger = nullptr;
    IConfig *config = nullptr;
    IAuth *auth = nullptr;
    ITime *time = nullptr;
    INetwork *network = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
    IRtspSessionReader *rtsp_session_reader = nullptr;
    IOnvifStatusReader *onvif_status_reader = nullptr;
    DeviceMedia *device = nullptr;
    IAiReader *ai = nullptr;
    IWebrtcStatusReader *webrtc_status_reader = nullptr;
    MediaStreams *media_streams = nullptr;
};

struct SystemHandlerDependencies {
    HttpAccess *access = nullptr;
    ISystem *system = nullptr;
    SystemOverviewSources overview;
};

struct MediaHandlerDependencies {
    HttpAccess *access = nullptr;
    IConfig *config = nullptr;
    DeviceMedia *device = nullptr;
    MediaStreams *media_streams = nullptr;
    IRtspSessionReader *rtsp_session_reader = nullptr;
    IWebrtcStatusReader *webrtc_status_reader = nullptr;
    IHttp *http = nullptr;
};

struct AiHandlerDependencies {
    HttpAccess *access = nullptr;
    IConfig *config = nullptr;
    IAiReader *ai = nullptr;
    DeviceMedia *device = nullptr;
};

struct SnapshotHandlerDependencies {
    HttpAccess *access = nullptr;
    DeviceMedia *device = nullptr;
};

std::unique_ptr<IHttpHandler> MakeAuthHandler(
    const AuthHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeConfigHandler(
    const ConfigHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeOperationsHandler(
    const OperationsHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeNetworkHandler(
    const NetworkHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeTimeHandler(
    const TimeHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeUpgradeHandler(
    const UpgradeHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeSystemHandler(
    const SystemHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeAlarmHandler(
    const AlarmHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeMediaHandler(
    const MediaHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeAiHandler(
    const AiHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeSnapshotHandler(
    const SnapshotHandlerDependencies &dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HANDLERS_HTTP_HANDLERS_H_
