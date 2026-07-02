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
class INetStat;
class DeviceMedia;
class IAiReader;
class IWebrtc;
class IWebrtcReader;
class IHttp;

struct SystemHandlerRefs {
    HttpAccess *access = nullptr;
    ISystem *system = nullptr;
    ITime *time = nullptr;
    INetwork *network = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
    IAiReader *ai = nullptr;
    DeviceMedia *device = nullptr;
};

struct MediaHandlerRefs {
    HttpAccess *access = nullptr;
    IConfig *config = nullptr;
    DeviceMedia *device = nullptr;
    MediaStreams *media_streams = nullptr;
    IRtspSessionReader *rtsp_session_reader = nullptr;
    IWebrtcReader *webrtc_reader = nullptr;
    INetStat *net_stat = nullptr;
    IHttp *http = nullptr;
};

struct AiHandlerRefs {
    HttpAccess *access = nullptr;
    IConfig *config = nullptr;
    IAiReader *ai = nullptr;
    DeviceMedia *device = nullptr;
};

std::unique_ptr<IHttpHandler> MakeAuthHandler(HttpAccess *access,
                                              IAuth *auth);
std::unique_ptr<IHttpHandler> MakeConfigHandler(HttpAccess *access,
                                                IConfig *config);
std::unique_ptr<IHttpHandler> MakeOperationsHandler(HttpAccess *access,
                                                    ILogger *logger);
std::unique_ptr<IHttpHandler> MakeNetworkHandler(HttpAccess *access,
                                                 INetwork *network);
std::unique_ptr<IHttpHandler> MakeTimeHandler(HttpAccess *access,
                                              ITime *time);
std::unique_ptr<IHttpHandler> MakeUpgradeHandler(HttpAccess *access,
                                                 IUpgrade *upgrade);
std::unique_ptr<IHttpHandler> MakeSystemHandler(
    const SystemHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeAlarmHandler(HttpAccess *access,
                                               IAlarm *alarm);
std::unique_ptr<IHttpHandler> MakeMediaHandler(
    const MediaHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeAiHandler(
    const AiHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeSnapshotHandler(HttpAccess *access,
                                                  DeviceMedia *device);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HANDLERS_HTTP_HANDLERS_H_
