#include "rtsp_rtp_sender.h"

#include "rtsp_transport.h"

#include <utility>

namespace live_stream {

class RtspRtpPacketSink final : public rtp::IRtpPacketSink {
 public:
  RtspRtpPacketSink(RtspRtpSender *sender,
                    std::shared_ptr<RtspSession> session,
                    const EncodedFrame *frame,
                    const RtspRtpSenderContext *context)
      : sender_(sender),
        session_(std::move(session)),
        frame_(frame),
        context_(context) {}

  bool OnRtpPacket(const rtp::RtpPacketView &packet) override {
    if (sender_ == nullptr || frame_ == nullptr || context_ == nullptr ||
        !ok_) {
      return false;
    }
    ok_ = sender_->SendRtpPacketView(session_, *frame_, packet, *context_);
    return ok_;
  }

 private:
  RtspRtpSender *sender_ = nullptr;
  std::shared_ptr<RtspSession> session_;
  const EncodedFrame *frame_ = nullptr;
  const RtspRtpSenderContext *context_ = nullptr;
  bool ok_ = true;
};

namespace {

bool IsKeyFrame(const EncodedFrame &frame) {
  return frame.frame_type == FrameType::kIdr ||
         frame.frame_type == FrameType::kI;
}

}  // namespace

RtspRtpSender::RtspRtpSender(uint32_t rtp_mtu_bytes)
    : packetizer_(rtp_mtu_bytes) {}

void RtspRtpSender::SendFrame(const std::shared_ptr<RtspSession> &session,
                              const EncodedFrame &frame,
                              const RtspRtpSenderContext &context) {
  if (session == nullptr || context.mutex == nullptr ||
      context.service_stats == nullptr) {
    return;
  }

  // 每个 RTSP session 必须从关键帧开始；如果 reader 溢出后重新等待关键帧，
  // 非关键帧直接丢弃，避免客户端解码参考帧缺失。
  bool should_drop = false;
  {
    std::lock_guard<std::mutex> lock(*context.mutex);
    if (!session->keyframe_seen) {
      if (!IsKeyFrame(frame)) {
        ++session->stats.dropped_frames;
        ++context.service_stats->dropped_frames;
        should_drop = true;
      } else {
        session->keyframe_seen = true;
      }
    }
  }
  if (should_drop) {
    return;
  }

  uint16_t sequence = 0;
  {
    std::lock_guard<std::mutex> lock(*context.mutex);
    sequence = session->rtp_sequence;
  }

  // Packetize 会按 MTU 拆出一个或多个 RTP packet，并通过 sink 立即发送；
  // sequence 在锁外推进，发送结束后再写回 session，减少锁持有时间。
  RtspRtpPacketSink sink(this, session, &frame, &context);
  (void)packetizer_.Packetize(frame, &sequence, session->ssrc, &sink);
  {
    std::lock_guard<std::mutex> lock(*context.mutex);
    session->rtp_sequence = sequence;
  }
}

bool RtspRtpSender::SendRtpPacketView(
    const std::shared_ptr<RtspSession> &session,
    const EncodedFrame &frame,
    const rtp::RtpPacketView &packet,
    const RtspRtpSenderContext &context) {
  if (session == nullptr || context.mutex == nullptr ||
      context.service_stats == nullptr) {
    return false;
  }

  RtspTransportTarget target;
  {
    std::lock_guard<std::mutex> lock(*context.mutex);
    // 发送前复制 transport 快照，避免持锁调用 net。SETUP/TEARDOWN 同时发生时，
    // 发送失败会走统一关闭路径。
    target.mode = session->transport;
    target.connection_id = session->connection_id;
    target.udp_socket_id = session->rtp_socket_id;
    target.udp_peer = session->peer;
    target.udp_peer.port = session->client_rtp_port;
    target.interleaved_rtp_channel = session->interleaved_rtp_channel;
  }

  const size_t packet_size = packet.Size();
  if (packet_size == 0 || packet_size > 0xffff) {
    return false;
  }
  const bool sent = RtspTransport::SendRtpPacket(
      context.net_engine, target, frame, packet);
  if (!sent) {
    // 发送失败通常表示 TCP 队列满、连接关闭或 UDP socket 不可用。这里按慢客户端
    // 处理并关闭控制连接，让 CloseSessionResources 释放 reader/UDP socket。
    {
      std::lock_guard<std::mutex> lock(*context.mutex);
      ++session->stats.dropped_frames;
      ++context.service_stats->dropped_frames;
    }
    {
      std::lock_guard<std::mutex> lock(*context.mutex);
      ++context.service_stats->slow_client_closes;
    }
    if (context.net_engine != nullptr) {
      (void)context.net_engine->Close(target.connection_id);
    }
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(*context.mutex);
    // pending_bytes 只对 TCP interleaved 有意义；UDP 下 net 返回 0 或当前诊断值。
    ++session->stats.sent_rtp_packets;
    session->stats.sent_rtp_bytes += packet_size;
    session->stats.pending_bytes =
        context.net_engine != nullptr
            ? context.net_engine->PendingBytes(target.connection_id)
            : 0;
    ++context.service_stats->sent_rtp_packets;
    context.service_stats->sent_rtp_bytes += packet_size;
  }
  return true;
}

}  // namespace live_stream
