#ifndef LIVE_STREAM_RTSP_SRC_RTSP_SESSION_CLOSE_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_SESSION_CLOSE_H_

#include "media/media_streams.h"
#include "rtsp_session.h"

#include <mutex>

namespace live_stream {

class ISocketIo;
class RtspSessionVideoSender;

class RtspSessionClose {
public:
    RtspSessionClose(ISocketIo *socket_io,
                     std::mutex *mutex,
                     RtspSessionVideoSender *video_sender);

    void CloseSessionVideoSend(RtspSession &session,
                               SubscriptionClose reason);

private:
    void CloseSessionUdp(RtspSession &session);

    ISocketIo *socket_io_ = nullptr;
    std::mutex *mutex_ = nullptr;
    RtspSessionVideoSender *video_sender_ = nullptr;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_SESSION_CLOSE_H_
