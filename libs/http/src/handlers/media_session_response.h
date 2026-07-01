#ifndef LIVE_STREAM_HTTP_SRC_HANDLERS_MEDIA_SESSION_RESPONSE_H_
#define LIVE_STREAM_HTTP_SRC_HANDLERS_MEDIA_SESSION_RESPONSE_H_

#include "http.h"
#include "json.h"
#include "rtsp.h"
#include "webrtc.h"

namespace live_stream {

class INetStat;
class MediaStreams;

Json BuildRtspSessionResponse(const RtspSessionInfo &session);
Json BuildWebrtcSessionResponse(const WebrtcPeerInfo &peer);
Json BuildHttpStreamingSessionResponse(const HttpStreamSessionInfo &session,
                                       MediaStreams *media_streams);
bool IsMediaStreamingSession(const HttpStreamSessionInfo &session);
void AddWebrtcStatsToResponse(Json *root, const WebrtcStats &stats);
void AddNetStatToResponse(Json *root, INetStat *net_stat);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HANDLERS_MEDIA_SESSION_RESPONSE_H_
