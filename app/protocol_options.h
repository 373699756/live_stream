#ifndef LIVE_STREAM_APP_PROTOCOL_OPTIONS_H_
#define LIVE_STREAM_APP_PROTOCOL_OPTIONS_H_

#include "protocol_runtime_refs.h"
#include "runtime_config.h"

#include "http.h"
#include "infra/executor.h"
#include "media_pipeline.h"
#include "net.h"
#include "net_adaptive.h"
#include "onvif_server.h"
#include "rtsp.h"
#include "webrtc.h"

namespace live_stream {

infra::ExecutorOptions BuildNetCallbackExecutorOptions();
NetEngineOptions BuildNetEngineOptions(infra::Executor *callback_executor);

RtspOptions BuildRtspOptions(const AppRuntimeConfig &runtime_config);
RtspDependencies BuildRtspDependencies(const ProtocolRuntimeRefs &refs);

WebrtcOptions BuildWebrtcOptions(const AppRuntimeConfig &runtime_config,
                                 const ProtocolRuntimeRefs &refs);
WebrtcDependencies BuildWebrtcDependencies(const ProtocolRuntimeRefs &refs);

MediaPipelineOptions BuildMediaPipelineOptions();
MediaPipelineDependencies BuildMediaPipelineDependencies(
    const ProtocolRuntimeRefs &refs);

OnvifServerOptions BuildOnvifOptions(
    const AppRuntimeConfig &runtime_config);
OnvifServerDependencies BuildOnvifDependencies(
    const ProtocolRuntimeRefs &refs);

HttpOptions BuildHttpOptions(const AppRuntimeConfig &runtime_config);

NetAdaptiveOptions BuildNetAdaptiveOptions();
NetAdaptiveDependencies BuildNetAdaptiveDependencies(
    const ProtocolRuntimeRefs &refs);

}  // namespace live_stream

#endif  // LIVE_STREAM_APP_PROTOCOL_OPTIONS_H_
