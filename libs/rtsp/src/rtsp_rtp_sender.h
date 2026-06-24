#ifndef LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_

#include "media/media_frame.h"
#include "net.h"
#include "rtsp.h"
#include "rtsp_session.h"
#include "rtp.h"

#include <memory>
#include <mutex>

namespace live_stream {

class RtspRtpPacketSink;

struct RtspRtpSenderContext {
    INetIo *net_io = nullptr;
    std::mutex *mutex = nullptr;
    RtspStats *service_stats = nullptr;
};

class RtspRtpSender {
public:
    explicit RtspRtpSender(uint32_t rtp_mtu_bytes);

    void SendFrame(const std::shared_ptr<RtspSession> &session,
                   const MediaFrame &frame,
                   const RtspRtpSenderContext &context);

private:
    friend class RtspRtpPacketSink;

    bool SendRtpPacketView(const std::shared_ptr<RtspSession> &session,
                           const MediaFrame &frame,
                           const rtp::RtpPacketView &packet,
                           const RtspRtpSenderContext &context);
    rtp::RtpPacketizer packetizer_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_
