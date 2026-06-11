#ifndef LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_OPTIONS_H_
#define LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_OPTIONS_H_

#include "config/runtime_config.h"
#include "subsystems/protocol_runtime_refs.h"

#include "http.h"
#include "infra/executor.h"
#include "net.h"
#include "net_stat.h"
#include "onvif_server.h"
#include "rtsp.h"
#include "webrtc.h"

namespace live_stream {

infra::ExecutorOptions BuildNetCallbackOptions();
NetEngineOptions BuildNetEngineOptions(infra::Executor *callback_executor);

RtspOptions BuildRtspOptions(const AppRuntimeConfig &runtime_config);
RtspDependencies BuildRtspDependencies(const ProtocolRuntimeRefs &refs);

WebrtcOptions BuildWebrtcOptions(const AppRuntimeConfig &runtime_config,
                                 const ProtocolRuntimeRefs &refs);
WebrtcDependencies BuildWebrtcDependencies(const ProtocolRuntimeRefs &refs);

OnvifServerOptions BuildOnvifOptions(
    const AppRuntimeConfig &runtime_config);
OnvifServerDependencies BuildOnvifDependencies(
    const ProtocolRuntimeRefs &refs);

HttpOptions BuildHttpOptions(const AppRuntimeConfig &runtime_config);
HttpDependencies BuildHttpDependencies(const ProtocolRuntimeRefs &refs);

NetStatOptions BuildNetStatOptions();
NetStatDependencies BuildNetStatDependencies(
    const ProtocolRuntimeRefs &refs);

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_SUBSYSTEMS_PROTOCOL_OPTIONS_H_
