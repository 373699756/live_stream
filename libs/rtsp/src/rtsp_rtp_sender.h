#ifndef LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_

#include "media/media_frame.h"
#include "socket_io.h"
#include "rtsp.h"
#include "rtsp_session.h"
#include "rtp.h"

#include <memory>
#include <mutex>

namespace live_stream {

class RtspRtpPacketSink;

struct RtspRtpSenderContext {
    ISocketIo &socket_io;
    std::mutex &mutex;
    RtspStats &service_stats;
};

class RtspRtpSender {
public:
    explicit RtspRtpSender(uint32_t rtp_mtu_bytes);

    void SendFrame(RtspSession &session, const MediaFrame &frame,
                   const RtspRtpSenderContext &context);

private:
    friend class RtspRtpPacketSink;

    bool SendRtpPacketView(RtspSession &session,
                           const MediaFrame &frame,
                           const rtp::RtpPacketView &packet,
                           const RtspRtpSenderContext &context);
    rtp::RtpPacketizer packetizer_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_
