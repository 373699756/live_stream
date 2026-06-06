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

std::unique_ptr<IHttp> CreateHttpConsole(
    const HttpOptions &options, NetEngine *net_engine,
    IAuth *auth, ILogger *logger,
    IConfig *config, INetworkConfig *network_config,
    ITime *time, IAlarm *alarm,
    IUpgrade *upgrade, ISystem *system,
    IRtsp *rtsp, OnvifServer *onvif,
    IAiView *ai, IDeviceMedia *device_media,
    ISnapshotView *snapshot, IWebrtc *webrtc,
    IMediaSource *media_source,
    IMediaFlvSource *media_flv_source,
    IMediaMjpegSource *media_mjpeg_source);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_HTTP_CONSOLE_H_
