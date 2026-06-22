#ifndef LIVE_STREAM_HTTP_HTTP_DEPENDENCIES_H_
#define LIVE_STREAM_HTTP_HTTP_DEPENDENCIES_H_

#include "http.h"
#include "media/media_streams.h"

namespace live_stream {

class INetEngine;
class INetExecutor;
class IAuth;
class IConfig;
class ILogger;
class IAlarm;
class INetwork;
class OnvifServer;
class IRtsp;
class ISystem;
class ITime;
class IUpgrade;
class IWebrtc;
class DeviceMedia;
class IAiView;
class IEvent;

struct HttpDependencies {
    INetEngine *net_engine = nullptr;
    INetExecutor *net_executor = nullptr;
    IAuth *auth = nullptr;
    ILogger *logger = nullptr;
    IConfig *config = nullptr;
    INetwork *network = nullptr;
    ITime *time = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
    ISystem *system = nullptr;
    IRtsp *rtsp = nullptr;
    OnvifServer *onvif = nullptr;
    IAiView *ai = nullptr;
    DeviceMedia *device = nullptr;
    IWebrtc *webrtc = nullptr;
    MediaStreams *media_streams = nullptr;
    IEvent *event = nullptr;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_HTTP_DEPENDENCIES_H_
