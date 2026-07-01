#include "rtsp_session_close.h"

#include "rtsp_session_video_sender.h"
#include "socket_io.h"

namespace live_stream {

RtspSessionClose::RtspSessionClose(ISocketIo *socket_io,
                                   std::mutex *mutex,
                                   RtspSessionVideoSender *video_sender)
    : socket_io_(socket_io),
      mutex_(mutex),
      video_sender_(video_sender) {}

void RtspSessionClose::CloseSessionResources(RtspSession &session,
                                             SubscriptionClose reason) {
    if (video_sender_ != nullptr) {
        video_sender_->CloseSubscription(session, reason);
    }
    CloseSessionUdp(session);
}

void RtspSessionClose::CloseSessionUdp(RtspSession &session) {
    UdpSocketId rtp_socket_id = 0;
    UdpSocketId rtcp_socket_id = 0;
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        rtp_socket_id = session.rtp_socket_id;
        rtcp_socket_id = session.rtcp_socket_id;
        session.rtp_socket_id = 0;
        session.rtcp_socket_id = 0;
    }
    // socket id 清零后再关闭 socket_io endpoint，避免关闭回调里再次找到同一 session
    // 并重复关闭相同 UDP socket。
    if (rtp_socket_id != 0) {
        (void)socket_io_->CloseUdp(rtp_socket_id);
    }
    if (rtcp_socket_id != 0) {
        (void)socket_io_->CloseUdp(rtcp_socket_id);
    }
}

}  // namespace live_stream
