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
class IOnvifReader;
class IRtspSessionReader;
class ISystem;
class ITime;
class IUpgrade;
class IWebrtc;
class IWebrtcReader;
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
    IOnvifReader *onvif_reader = nullptr;
    IAiReader *ai = nullptr;
    DeviceMedia *device = nullptr;
    IWebrtc *webrtc = nullptr;
    IWebrtcReader *webrtc_reader = nullptr;
    MediaStreams *media_streams = nullptr;
    event::Dispatcher *event = nullptr;
};

struct HttpControlDependencies {
    IAuth *auth = nullptr;
    ILogger *logger = nullptr;
    IConfig *config = nullptr;
    INetwork *network = nullptr;
    ITime *time = nullptr;
    IAlarm *alarm = nullptr;
    IUpgrade *upgrade = nullptr;
    ISystem *system = nullptr;
    IRtspSessionReader *rtsp_session_reader = nullptr;
    IOnvifReader *onvif_reader = nullptr;
    IAiReader *ai = nullptr;
    DeviceMedia *device = nullptr;
    IWebrtcReader *webrtc_reader = nullptr;
    MediaStreams *media_streams = nullptr;
};

struct HttpMediaDependencies {
    IConfig *config = nullptr;
    DeviceMedia *device = nullptr;
    MediaStreams *media_streams = nullptr;
    IRtspSessionReader *rtsp_session_reader = nullptr;
    IWebrtcReader *webrtc_reader = nullptr;
    IWebrtc *webrtc = nullptr;
};

struct HttpStreamingDependencies {
    DeviceMedia *device = nullptr;
    MediaStreams *media_streams = nullptr;
    event::Dispatcher *event = nullptr;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_HTTP_DEPENDENCIES_H_
