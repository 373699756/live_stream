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
class IOnvifReader;
class DeviceMedia;
class IAiReader;
class IWebrtc;
class IWebrtcReader;
class IHttp;

struct AuthHandlerRefs {
    HttpAccess *access = nullptr;
    IAuth *auth = nullptr;
};

struct ConfigHandlerRefs {
    HttpAccess *access = nullptr;
    IConfig *config = nullptr;
};

struct OperationsHandlerRefs {
    HttpAccess *access = nullptr;
    ILogger *logger = nullptr;
};

struct NetworkHandlerRefs {
    HttpAccess *access = nullptr;
    INetwork *network = nullptr;
};

struct TimeHandlerRefs {
    HttpAccess *access = nullptr;
    ITime *time = nullptr;
};

struct UpgradeHandlerRefs {
    HttpAccess *access = nullptr;
    IUpgrade *upgrade = nullptr;
};

struct AlarmHandlerRefs {
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
    IOnvifReader *onvif_reader = nullptr;
    DeviceMedia *device = nullptr;
    IAiReader *ai = nullptr;
    IWebrtcReader *webrtc_reader = nullptr;
    MediaStreams *media_streams = nullptr;
};

struct SystemHandlerRefs {
    HttpAccess *access = nullptr;
    ISystem *system = nullptr;
    SystemOverviewSources overview;
};

struct MediaHandlerRefs {
    HttpAccess *access = nullptr;
    IConfig *config = nullptr;
    DeviceMedia *device = nullptr;
    MediaStreams *media_streams = nullptr;
    IRtspSessionReader *rtsp_session_reader = nullptr;
    IWebrtcReader *webrtc_reader = nullptr;
    IHttp *http = nullptr;
};

struct AiHandlerRefs {
    HttpAccess *access = nullptr;
    IConfig *config = nullptr;
    IAiReader *ai = nullptr;
    DeviceMedia *device = nullptr;
};

struct SnapshotHandlerRefs {
    HttpAccess *access = nullptr;
    DeviceMedia *device = nullptr;
};

std::unique_ptr<IHttpHandler> MakeAuthHandler(
    const AuthHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeConfigHandler(
    const ConfigHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeOperationsHandler(
    const OperationsHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeNetworkHandler(
    const NetworkHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeTimeHandler(
    const TimeHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeUpgradeHandler(
    const UpgradeHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeSystemHandler(
    const SystemHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeAlarmHandler(
    const AlarmHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeMediaHandler(
    const MediaHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeAiHandler(
    const AiHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeSnapshotHandler(
    const SnapshotHandlerRefs &refs);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HANDLERS_HTTP_HANDLERS_H_
