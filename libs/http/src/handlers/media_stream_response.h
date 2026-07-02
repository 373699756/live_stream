#ifndef LIVE_STREAM_HTTP_SRC_HANDLERS_MEDIA_STREAM_RESPONSE_H_
#define LIVE_STREAM_HTTP_SRC_HANDLERS_MEDIA_STREAM_RESPONSE_H_

#include "hisi_vendor/media_capabilities.h"
#include "json.h"
#include "media/stream_types.h"

namespace live_stream {

class IConfig;
class MediaStreams;
struct WebrtcStats;

Json BuildMediaStreamResponse(StreamId stream_id,
                              IConfig *config,
                              MediaStreams *media_streams,
                              const WebrtcStats &webrtc_stats);
Json BuildMediaCapabilitiesResponse(const MediaCapabilities &capabilities);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HANDLERS_MEDIA_STREAM_RESPONSE_H_
