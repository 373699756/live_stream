#ifndef LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_OPTIONS_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_OPTIONS_H_

#include "config/app_config.h"
#include "subsystems/protocol_refs.h"

#include "http.h"
#include "net.h"
#include "net_stat.h"
#include "onvif_server.h"
#include "rtsp.h"
#include "webrtc.h"

namespace live_stream {

class FoundationSubsystem;

event::LoopOptions BuildNetCallbackOptions();
NetIoOptions BuildNetIoOptions(event::Loop *callback_loop);

RtspOptions BuildRtspOptions(const AppConfig &app_config);
RtspDependencies BuildRtspDependencies(const ProtocolStartupRefs &refs,
                                       FoundationSubsystem &foundation);

WebrtcOptions BuildWebrtcOptions(const AppConfig &app_config,
                                 const ProtocolStartupRefs &refs);
WebrtcDependencies BuildWebrtcDependencies(const ProtocolStartupRefs &refs,
                                           FoundationSubsystem &foundation);

OnvifServerOptions BuildOnvifOptions(
    const AppConfig &app_config);
OnvifServerDependencies BuildOnvifDependencies(
    const ProtocolStartupRefs &refs,
    FoundationSubsystem &foundation);

HttpOptions BuildHttpOptions(const AppConfig &app_config);
HttpDependencies BuildHttpDependencies(const ProtocolStartupRefs &refs,
                                       FoundationSubsystem &foundation);

NetStatOptions BuildNetStatOptions();

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_OPTIONS_H_
