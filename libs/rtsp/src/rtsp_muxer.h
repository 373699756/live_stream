#ifndef LIVE_STREAM_RTSP_SRC_RTSP_MUXER_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_MUXER_H_

#include "media/media_streams.h"
#include "rtsp.h"

#include <string>

namespace live_stream {

class RtspMuxer {
public:
    static std::string BuildSdp(const RtspListenAddress &address,
                                StreamId stream_id,
                                const MediaStreamInfo &stream_info);
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_MUXER_H_
