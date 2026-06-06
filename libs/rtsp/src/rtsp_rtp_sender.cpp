#include "rtsp_rtp_sender.h"

#include "rtsp_transport.h"

#include <utility>

namespace live_stream {

class RtspRtpPacketSink final : public media_mux::IRtpPacketSink {
 public:
  RtspRtpPacketSink(RtspRtpSender *sender,
                    std::shared_ptr<RtspSession> session,
                    const EncodedFrame *frame,
                    const RtspRtpSenderContext *context)
      : sender_(sender),
        session_(std::move(session)),
        frame_(frame),
        context_(context) {}

  bool OnRtpPacket(const media_mux::RtpPacketView &packet) override {
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
    NotifyAdaptive(context, *session, RtspAdaptiveEventType::kFrameDropped);
    return;
  }

  uint16_t sequence = 0;
  {
    std::lock_guard<std::mutex> lock(*context.mutex);
    sequence = session->rtp_sequence;
  }

  RtspRtpPacketSink sink(this, session, &frame, &context);
  const bool packetized =
      packetizer_.Packetize(frame, &sequence, session->ssrc, &sink);
  {
    std::lock_guard<std::mutex> lock(*context.mutex);
    session->rtp_sequence = sequence;
  }
  if (!packetized) {
    NotifyAdaptive(context, *session, RtspAdaptiveEventType::kFrameDropped);
  }
}

bool RtspRtpSender::SendRtpPacketView(
    const std::shared_ptr<RtspSession> &session,
    const EncodedFrame &frame,
    const media_mux::RtpPacketView &packet,
    const RtspRtpSenderContext &context) {
  if (session == nullptr || context.mutex == nullptr ||
      context.service_stats == nullptr) {
    return false;
  }

  RtspTransportTarget target;
  {
    std::lock_guard<std::mutex> lock(*context.mutex);
    target.mode = session->transport;
    target.connection_id = session->connection_id;
    target.udp_socket_id =
        context.udp_socket_id != nullptr ? *context.udp_socket_id : 0;
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
    {
      std::lock_guard<std::mutex> lock(*context.mutex);
      ++session->stats.dropped_frames;
      ++context.service_stats->dropped_frames;
    }
    NotifyAdaptive(context, *session, RtspAdaptiveEventType::kFrameDropped);
    {
      std::lock_guard<std::mutex> lock(*context.mutex);
      ++context.service_stats->slow_client_closes;
    }
    NotifyAdaptive(context, *session, RtspAdaptiveEventType::kSlowClientClosed);
    if (context.net_engine != nullptr) {
      (void)context.net_engine->Close(target.connection_id);
    }
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(*context.mutex);
    ++session->stats.sent_rtp_packets;
    session->stats.sent_rtp_bytes += packet_size;
    session->stats.pending_bytes =
        context.net_engine != nullptr
            ? context.net_engine->PendingBytes(target.connection_id)
            : 0;
    ++context.service_stats->sent_rtp_packets;
    context.service_stats->sent_rtp_bytes += packet_size;
  }
  NotifyAdaptive(context, *session, RtspAdaptiveEventType::kSample);
  return true;
}

void RtspRtpSender::NotifyAdaptive(const RtspRtpSenderContext &context,
                                   const RtspSession &session,
                                   RtspAdaptiveEventType event) const {
  if (context.adaptive_observer == nullptr || context.mutex == nullptr) {
    return;
  }
  RtspAdaptiveSample sample;
  sample.event = event;
  {
    std::lock_guard<std::mutex> lock(*context.mutex);
    sample.session = session.stats;
  }
  (void)context.adaptive_observer->OnRtspAdaptiveSample(sample);
}

}  // namespace live_stream
