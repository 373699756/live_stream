#ifndef LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_REFS_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_REFS_H_

#include "subsystems/device_subsystem.h"
#include "subsystems/media_subsystem.h"

#include "http.h"
#include "socket_io.h"
#include "onvif_server.h"
#include "rtsp.h"
#include "webrtc.h"

namespace live_stream {

struct ProtocolStartupRefs {
    DeviceRefs device;
    MediaRefs media;
    ISocketIo *socket_io = nullptr;
    event::Loop *rtsp_loop = nullptr;
    event::Loop *webrtc_loop = nullptr;
    event::Loop *onvif_loop = nullptr;
    event::Loop *http_loop = nullptr;
    IRtsp *rtsp = nullptr;
    OnvifServer *onvif = nullptr;
    IWebrtc *webrtc = nullptr;
    IHttp *http = nullptr;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_REFS_H_
