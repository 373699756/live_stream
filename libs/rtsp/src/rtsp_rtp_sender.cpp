#include "rtsp_rtp_sender.h"

#include "rtsp_transport.h"

namespace live_stream {

class RtspRtpPacketSink final : public rtp::IRtpPacketSink {
public:
    RtspRtpPacketSink(RtspRtpSender &sender,
                      RtspSession &session,
                      const MediaFrame &frame,
                      const RtspRtpSenderContext &context)
        : sender_(sender),
          session_(session),
          frame_(frame),
          context_(context) {}

    bool OnRtpPacket(const rtp::RtpPacketView &packet) override {
        if (!ok_) {
            return false;
        }
        ok_ = sender_.SendRtpPacketView(session_, frame_, packet, context_);
        return ok_;
    }

private:
    RtspRtpSender &sender_;
    RtspSession &session_;
    const MediaFrame &frame_;
    const RtspRtpSenderContext &context_;
    bool ok_ = true;
};

namespace {

bool IsKeyframe(const MediaFrame &frame) {
    return frame.frame_type == FrameType::kIdr ||
           frame.frame_type == FrameType::kI;
}

bool RtpCodecFromCodec(Codec codec, rtp::Codec &rtp_codec) {
    if (codec == Codec::kH264) {
        rtp_codec = rtp::Codec::kH264;
        return true;
    }
    if (codec == Codec::kH265) {
        rtp_codec = rtp::Codec::kH265;
        return true;
    }
    return false;
}

bool BuildRtpInput(const MediaFrame &frame, uint16_t &sequence,
                   uint32_t ssrc, rtp::RtpPacketizerInput &input) {
    if (!IsMediaFramePayloadValid(frame)) {
        return false;
    }
    rtp::Codec rtp_codec = rtp::Codec::kH264;
    if (!RtpCodecFromCodec(frame.codec, rtp_codec)) {
        return false;
    }
    input.codec = rtp_codec;
    input.payload = MediaFramePayloadData(frame);
    input.payload_size = frame.payload.Size();
    input.pts_us = frame.pts_us;
    input.sequence = &sequence;
    input.ssrc = ssrc;
    input.payload_type = rtp::RtpPayloadTypeForCodec(input.codec);
    return input.payload != nullptr && input.payload_size > 0;
}

}  // namespace

RtspRtpSender::RtspRtpSender(uint32_t rtp_mtu_bytes)
    : packetizer_(rtp_mtu_bytes) {}

void RtspRtpSender::SendFrame(RtspSession &session, const MediaFrame &frame,
                              const RtspRtpSenderContext &context) {
    // 每个 RTSP session 必须从关键帧开始；如果 subscription 溢出后重新等待关键帧，
    // 非关键帧直接丢弃，避免客户端解码参考帧缺失。
    bool should_drop = false;
    {
        std::lock_guard<std::mutex> lock(context.mutex);
        if (!session.keyframe_seen) {
            if (!IsKeyframe(frame)) {
                ++session.stats.dropped_frames;
                ++context.service_stats.dropped_frames;
                should_drop = true;
            } else {
                session.keyframe_seen = true;
            }
        }
    }
    if (should_drop) {
        return;
    }

    uint16_t sequence = 0;
    {
        std::lock_guard<std::mutex> lock(context.mutex);
        sequence = session.rtp_sequence;
    }

    // Packetize 会按 MTU 拆出一个或多个 RTP packet，并通过 sink 立即发送；
    // sequence 在锁外推进，发送结束后再写回 session，减少锁持有时间。
    RtspRtpPacketSink sink(*this, session, frame, context);
    rtp::RtpPacketizerInput input;
    if (BuildRtpInput(frame, sequence, session.ssrc, input)) {
        (void)packetizer_.Packetize(input, &sink);
    }
    {
        std::lock_guard<std::mutex> lock(context.mutex);
        session.rtp_sequence = sequence;
    }
}

bool RtspRtpSender::SendRtpPacketView(
    RtspSession &session,
    const MediaFrame &frame,
    const rtp::RtpPacketView &packet,
    const RtspRtpSenderContext &context) {
    RtspRtpRoute route;
    {
        std::lock_guard<std::mutex> lock(context.mutex);
        // 发送前复制 transport 快照，避免持锁调用 socket_io。SETUP/TEARDOWN 同时发生时，
        // 发送失败会走统一关闭路径。
        route.mode = session.transport;
        route.connection_id = session.connection_id;
        route.udp_socket_id = session.rtp_socket_id;
        route.udp_peer = session.udp_rtp_peer;
        route.interleaved_rtp_channel = session.interleaved_rtp_channel;
    }

    const size_t packet_size = packet.Size();
    if (packet_size == 0 || packet_size > 0xffff) {
        return false;
    }
    const bool sent = RtspTransport::SendRtpPacket(
        context.socket_io, route, frame, packet);
    if (!sent) {
        // 发送失败通常表示 TCP 队列满、连接关闭或 UDP socket 不可用。这里按慢客户端
        // 处理并关闭控制连接，让 CloseSessionVideoSend 释放 subscription/UDP socket。
        {
            std::lock_guard<std::mutex> lock(context.mutex);
            ++session.stats.dropped_frames;
            ++context.service_stats.dropped_frames;
        }
        {
            std::lock_guard<std::mutex> lock(context.mutex);
            ++context.service_stats.slow_client_closes;
        }
        (void)context.socket_io.Close(route.connection_id);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(context.mutex);
        // pending_bytes 只对 TCP interleaved 有意义；UDP 下 socket_io 返回 0 或当前诊断值。
        ++session.stats.sent_rtp_packets;
        session.stats.sent_rtp_bytes += packet_size;
        session.stats.pending_bytes =
            context.socket_io.PendingBytes(route.connection_id);
        ++context.service_stats.sent_rtp_packets;
        context.service_stats.sent_rtp_bytes += packet_size;
    }
    return true;
}

}  // namespace live_stream
