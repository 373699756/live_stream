#ifndef LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_REFS_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_REFS_H_

#include "subsystems/device_subsystem.h"
#include "subsystems/media_subsystem.h"

#include "http.h"
#include "net.h"
#include "onvif_server.h"
#include "rtsp.h"
#include "webrtc.h"

namespace live_stream {

class CoreSubsystem;

struct ProtocolStartupRefs {
    CoreSubsystem *core = nullptr;
    DeviceRefs device;
    MediaRefs media;
    INetEngine *net_engine = nullptr;
    INetExecutor *rtsp_executor = nullptr;
    INetExecutor *webrtc_executor = nullptr;
    INetExecutor *onvif_executor = nullptr;
    INetExecutor *http_executor = nullptr;
    IRtsp *rtsp = nullptr;
    OnvifServer *onvif = nullptr;
    IWebrtc *webrtc = nullptr;
    IHttp *http = nullptr;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_REFS_H_
