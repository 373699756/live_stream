#ifndef LIVE_STREAM_RTSP_SRC_RTSP_SESSION_VIDEO_SENDER_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_SESSION_VIDEO_SENDER_H_

#include "media/media_streams.h"
#include "rtsp.h"
#include "rtsp_rtp_sender.h"
#include "rtsp_session.h"

#include <memory>
#include <mutex>

namespace live_stream {

namespace event {
class Loop;
}

class ISocketIo;

class RtspSessionVideoSender {
public:
    RtspSessionVideoSender(MediaStreams *media_streams,
                           event::Loop *net_loop,
                           ISocketIo *socket_io,
                           std::mutex *mutex,
                           RtspStats *stats,
                           uint32_t rtp_mtu_bytes);

    int StartMediaStream(RtspSession &session);
    void StartMediaSend(const std::shared_ptr<RtspSession> &session);
    void CloseSubscription(RtspSession &session, SubscriptionClose reason);

private:
    void StartSessionSendTimer(const std::shared_ptr<RtspSession> &session);
    void SendSessionFrames(const std::shared_ptr<RtspSession> &session);
    bool SendSessionStartFrames(const std::shared_ptr<RtspSession> &session,
                                uint32_t *sent_frames);
    void SendFrame(const std::shared_ptr<RtspSession> &session,
                   const MediaFrame &frame);
    RtspRtpSenderContext RtpSenderContext();

    MediaStreams *media_streams_ = nullptr;
    event::Loop *net_loop_ = nullptr;
    ISocketIo *socket_io_ = nullptr;
    std::mutex *mutex_ = nullptr;
    RtspStats *stats_ = nullptr;
    RtspRtpSender rtp_sender_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_SESSION_VIDEO_SENDER_H_
