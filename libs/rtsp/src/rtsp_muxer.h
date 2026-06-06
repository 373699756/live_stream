#ifndef LIVE_STREAM_RTSP_SRC_RTSP_MUXER_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_MUXER_H_

#include "media_source.h"
#include "rtsp.h"

#include <string>

namespace live_stream {

class RtspMuxer {
public:
    static std::string BuildSdp(const RtspListenAddress &address,
                                const MediaTrack &track);
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_MUXER_H_
