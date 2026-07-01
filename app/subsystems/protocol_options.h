#ifndef LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_OPTIONS_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_OPTIONS_H_

#include "config/app_config.h"
#include "subsystems/protocol_refs.h"

#include "http.h"
#include "socket_io.h"
#include "net_stat.h"
#include "onvif_server.h"
#include "rtsp.h"
#include "webrtc.h"

namespace live_stream {

class FoundationSubsystem;

event::LoopOptions BuildSocketCallbackOptions();
SocketIoOptions BuildSocketIoOptions(event::Loop *callback_loop);

RtspOptions BuildRtspOptions(const AppConfig &app_config);

WebrtcOptions BuildWebrtcOptions(const AppConfig &app_config,
                                 const ProtocolStartupRefs &refs);

OnvifServerOptions BuildOnvifOptions(
    const AppConfig &app_config);

HttpOptions BuildHttpOptions(const AppConfig &app_config);

NetStatOptions BuildNetStatOptions();

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_OPTIONS_H_
