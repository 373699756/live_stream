#ifndef LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_

#include "media/media_frame.h"
#include "socket_io.h"
#include "rtsp.h"
#include "rtsp_session.h"
#include "rtp.h"

#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace live_stream {

struct RtspRtpSendRefs {
    ISocketIo &socket_io;
    std::mutex &mutex;
    RtspStats &service_stats;
};

class RtspRtpSender {
public:
    explicit RtspRtpSender(uint32_t rtp_mtu_bytes);

    void SendFrame(RtspSession &session, const MediaFrame &frame,
                   const RtspRtpSendRefs &refs);

private:
    friend class RtpFrameBuilder;

    struct RtpPacketInfo {
        uint8_t payload_header[rtp::kMaxRtpPayloadHeaderSize] = {};
        size_t payload_header_size = 0;
        size_t payload_offset = 0;
        size_t payload_size = 0;
        bool marker = false;
        uint8_t payload_type = 0;
        uint32_t timestamp = 0;
    };

    struct RtpFrame {
        StreamId stream_id = StreamId::kMain;
        Codec codec = Codec::kH264;
        FrameSequence frame_sequence = 0;
        int64_t pts_us = 0;
        int64_t dts_us = 0;
        size_t payload_size = 0;
        MediaFrame frame;
        std::vector<RtpPacketInfo> packets;
    };

    bool SendRtpPacketView(RtspSession &session,
                           const MediaFrame &frame,
                           const rtp::RtpPacketView &packet,
                           const RtspRtpSendRefs &refs);
    std::shared_ptr<const RtpFrame> GetRtpFrame(const MediaFrame &frame);
    std::shared_ptr<const RtpFrame> FindRtpFrameLocked(
        const MediaFrame &frame) const;
    void AddRtpFrameLocked(
        const std::shared_ptr<const RtpFrame> &rtp_frame);
    static bool IsSameRtpFrame(
        const RtpFrame &rtp_frame,
        const MediaFrame &frame);

    rtp::RtpPacketizer packetizer_;
    mutable std::mutex rtp_frame_mutex_;
    std::deque<std::shared_ptr<const RtpFrame>> main_rtp_frames_;
    std::deque<std::shared_ptr<const RtpFrame>> sub_rtp_frames_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_
