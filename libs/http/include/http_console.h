#ifndef LIVE_STREAM_HTTP_HTTP_CONSOLE_H_
#define LIVE_STREAM_HTTP_HTTP_CONSOLE_H_

#include "http.h"
#include "media_source.h"

#include <memory>

namespace live_stream {

class IAuth;
class IConfig;
class ILogger;
class IAlarm;
class INetworkConfig;
class OnvifServer;
class IRtsp;
class ISystem;
class ITime;
class IUpgrade;
class IWebrtc;
class IDeviceMedia;
class IAiView;
class ISnapshotView;
class NetEngine;

struct HttpConsoleDependencies {
    NetEngine *net_engine = nullptr;
    IAuth *auth = nullptr;
    ILogger *logger = nullptr;
    IConfig *config = nullptr;
    INetworkConfig *network_config = nullptr;
    ITime *time = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
    ISystem *system = nullptr;
    IRtsp *rtsp = nullptr;
    OnvifServer *onvif = nullptr;
    IAiView *ai = nullptr;
    IDeviceMedia *device_media = nullptr;
    ISnapshotView *snapshot = nullptr;
    IWebrtc *webrtc = nullptr;
    IMediaSource *media_source = nullptr;
    IMediaFlvSource *media_flv_source = nullptr;
    IMediaMjpegSource *media_mjpeg_source = nullptr;
};

std::unique_ptr<IHttp> CreateHttpConsole(
    const HttpOptions &options,
    const HttpConsoleDependencies &dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_HTTP_CONSOLE_H_
