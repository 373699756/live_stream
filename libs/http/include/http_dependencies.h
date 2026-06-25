#ifndef LIVE_STREAM_HTTP_HTTP_DEPENDENCIES_H_
#define LIVE_STREAM_HTTP_HTTP_DEPENDENCIES_H_

#include "event.h"
#include "http.h"
#include "media/media_streams.h"

namespace live_stream {

class INetIo;
class IAuth;
class IConfig;
class ILogger;
class IAlarm;
class INetwork;
class IOnvifStatusReader;
class IRtspSessionReader;
class ISystem;
class ITime;
class IUpgrade;
class IWebrtc;
class IWebrtcStatusReader;
class DeviceMedia;
class IAiReader;

struct HttpDependencies {
    INetIo *net_io = nullptr;
    event::Loop *net_loop = nullptr;
    IAuth *auth = nullptr;
    ILogger *logger = nullptr;
    IConfig *config = nullptr;
    INetwork *network = nullptr;
    ITime *time = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
    ISystem *system = nullptr;
    IRtspSessionReader *rtsp_session_reader = nullptr;
    IOnvifStatusReader *onvif_status_reader = nullptr;
    IAiReader *ai = nullptr;
    DeviceMedia *device = nullptr;
    IWebrtc *webrtc = nullptr;
    IWebrtcStatusReader *webrtc_status_reader = nullptr;
    MediaStreams *media_streams = nullptr;
    event::Dispatcher *event = nullptr;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_HTTP_DEPENDENCIES_H_
