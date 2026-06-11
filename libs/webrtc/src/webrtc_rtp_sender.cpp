#include "webrtc_rtp_sender.h"

#include "webrtc_engine.h"

#include <cstdint>

namespace live_stream {
namespace webrtc_internal {

class WebrtcRtpPacketSink final : public rtp::IRtpPacketSink {
public:
    WebrtcRtpPacketSink(WebrtcRtpSender *sender,
                        const WebrtcPeerInfo *peer,
                        const EncodedFrame *frame,
                        const WebrtcRtpSenderContext *context)
        : sender_(sender),
          peer_(peer),
          frame_(frame),
          context_(context) {}

    bool OnRtpPacket(const rtp::RtpPacketView &packet) override {
        if (sender_ == nullptr || peer_ == nullptr || frame_ == nullptr ||
            context_ == nullptr || !ok_) {
            return false;
        }
        ok_ = sender_->SendRtpPacketView(*peer_, *frame_, packet, *context_);
        return ok_;
    }

private:
    WebrtcRtpSender *sender_ = nullptr;
    const WebrtcPeerInfo *peer_ = nullptr;
    const EncodedFrame *frame_ = nullptr;
    const WebrtcRtpSenderContext *context_ = nullptr;
    bool ok_ = true;
};

namespace {

bool IsKeyFrame(const MediaFrame &frame) {
    return frame.key_frame ||
           frame.encoded_frame.frame_type == FrameType::kIdr ||
           frame.encoded_frame.frame_type == FrameType::kI;
}

bool IsSupportedCodec(Codec codec) {
    return codec == Codec::kH264 || codec == Codec::kH265;
}

rtp::Codec RtpCodecFromCodec(Codec codec) {
    return codec == Codec::kH265 ? rtp::Codec::kH265
                                      : rtp::Codec::kH264;
}

}  // namespace

WebrtcRtpSender::WebrtcRtpSender(uint32_t rtp_mtu_bytes)
    : packetizer_(rtp_mtu_bytes) {}

void WebrtcRtpSender::AddPeer(const WebrtcPeerInfo &peer) {
    if (peer.peer_id.empty() || !IsSupportedCodec(peer.codec)) {
        return;
    }
    PeerRtpState state;
    state.sequence = 1;
    state.codec = peer.codec;
    state.keyframe_seen = false;
    peers_[peer.peer_id] = state;
}

void WebrtcRtpSender::RemovePeer(const std::string &peer_id) {
    peers_.erase(peer_id);
}

void WebrtcRtpSender::Clear() {
    peers_.clear();
}

bool WebrtcRtpSender::SendFrame(const WebrtcPeerInfo &peer,
                                const MediaFrame &frame,
                                const WebrtcRtpSenderContext &context) {
    if (context.mutex == nullptr || context.service_stats == nullptr ||
        !context.engine || peer.peer_id.empty() ||
        frame.stream_id != peer.stream_id || frame.codec != peer.codec ||
        !EncodedFrameHasPayload(&frame.encoded_frame)) {
        return false;
    }

    const bool frame_is_keyframe = IsKeyFrame(frame);
    uint16_t sequence = 0;
    WebrtcRtpSendParameters parameters;
    if (!context.engine->GetRtpSendParameters(peer.peer_id, &parameters) ||
        parameters.codec != frame.codec || parameters.payload_type == 0 ||
        parameters.clock_rate != rtp::kRtpClockRate ||
        parameters.ssrc == 0) {
        std::lock_guard<std::mutex> guard(*context.mutex);
        ++context.service_stats->dropped_frames;
        return false;
    }
    const uint32_t rtp_timestamp =
        rtp::RtpTimestampFromPtsUs(frame.pts_us, parameters.clock_rate);

    {
        std::lock_guard<std::mutex> guard(*context.mutex);
        auto iter = peers_.find(peer.peer_id);
        if (iter == peers_.end()) {
            return false;
        }
        PeerRtpState &state = iter->second;
        if (state.codec != frame.codec) {
            ++context.service_stats->dropped_frames;
            return false;
        }
        state.ssrc = parameters.ssrc;
        state.payload_type = parameters.payload_type;
        state.clock_rate = parameters.clock_rate;
        if (state.has_last_rtp_timestamp &&
            rtp::IsRtpTimestampBackwards(rtp_timestamp,
                                         state.last_rtp_timestamp)) {
            ++context.service_stats->dropped_frames;
            return false;
        }
        if (!state.keyframe_seen) {
            if (!frame_is_keyframe) {
                ++context.service_stats->dropped_frames;
                return false;
            }
        }
        sequence = state.sequence;
    }

    WebrtcRtpPacketSink sink(this, &peer, &frame.encoded_frame, &context);
    rtp::RtpPacketizerInput input;
    const auto payload = EncodedFramePayloadSlice(&frame.encoded_frame);
    input.codec = RtpCodecFromCodec(frame.codec);
    // WebRTC 发送使用 MediaFrame 持有的 EncodedFrame payload；RTP packetizer
    // 输出 packet view，不在分包阶段复制整帧。
    input.payload = EncodedFramePayloadData(&frame.encoded_frame);
    input.payload_size = payload.size;
    input.pts_us = frame.pts_us;
    input.sequence = &sequence;
    input.ssrc = parameters.ssrc;
    input.payload_type = parameters.payload_type;
    const bool packetized = packetizer_.Packetize(input, &sink);

    {
        std::lock_guard<std::mutex> guard(*context.mutex);
        auto iter = peers_.find(peer.peer_id);
        if (iter != peers_.end()) {
            iter->second.sequence = sequence;
            if (packetized && frame_is_keyframe) {
                iter->second.keyframe_seen = true;
            }
            if (packetized) {
                iter->second.last_rtp_timestamp = rtp_timestamp;
                iter->second.has_last_rtp_timestamp = true;
            }
        }
        if (!packetized) {
            ++context.service_stats->dropped_frames;
        } else {
            ++context.service_stats->sent_frames;
        }
    }
    return packetized;
}

bool WebrtcRtpSender::SendRtpPacketView(
    const WebrtcPeerInfo &peer,
    const EncodedFrame &frame,
    const rtp::RtpPacketView &packet,
    const WebrtcRtpSenderContext &context) {
    if (!context.engine || context.mutex == nullptr ||
        context.service_stats == nullptr || packet.Size() == 0) {
        return false;
    }

    // packet view 中的媒体 slice 只在当前调用栈有效。engine/transport 会在
    // SRTP protect 阶段复制成加密后的连续 UDP packet。
    const bool sent = context.engine->SendRtpPacket(peer, frame, packet);
    {
        std::lock_guard<std::mutex> guard(*context.mutex);
        if (sent) {
            ++context.service_stats->sent_rtp_packets;
        } else {
            ++context.service_stats->dropped_rtp_packets;
        }
    }
    return sent;
}

}  // namespace webrtc_internal
}  // namespace live_stream
