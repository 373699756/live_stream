#include "rtsp_rtp_sender.h"

#include "byte_writer.h"
#include "infra/time.h"
#include "rtsp_transport.h"

#include <cstdint>
#include <cstring>
#include <utility>

namespace live_stream {

class RtpFrameBuilder final : public rtp::IRtpPacketSink {
public:
    explicit RtpFrameBuilder(const MediaFrame &frame)
        : frame_(frame) {}

    bool OnRtpPacket(const rtp::RtpPacketView &packet) override {
        const uint8_t *payload = MediaFramePayloadData(frame_);
        const size_t payload_size = frame_.payload.Size();
        if (payload == nullptr || payload_size == 0) {
            return false;
        }

        RtspRtpSender::RtpPacketInfo rtp_packet;
        rtp_packet.marker = packet.marker;
        rtp_packet.payload_type = packet.payload_type;
        rtp_packet.timestamp = packet.timestamp;

        bool has_media_payload = false;
        const uintptr_t payload_begin = reinterpret_cast<uintptr_t>(payload);
        const uintptr_t payload_end = payload_begin + payload_size;
        for (size_t i = 0; i < packet.slice_size; ++i) {
            const rtp::RtpPacketSlice &slice = packet.slices[i];
            if (slice.media_payload) {
                const uintptr_t slice_begin =
                    reinterpret_cast<uintptr_t>(slice.data);
                const uintptr_t slice_end = slice_begin + slice.size;
                if (slice_end < slice_begin ||
                    slice_begin < payload_begin ||
                    slice_end > payload_end) {
                    return false;
                }
                rtp_packet.payload_offset =
                    static_cast<size_t>(slice_begin - payload_begin);
                rtp_packet.payload_size = slice.size;
                has_media_payload = true;
                continue;
            }
            if (slice.data == packet.rtp_header) {
                continue;
            }
            if (slice.size > sizeof(rtp_packet.payload_header)) {
                return false;
            }
            std::memcpy(rtp_packet.payload_header, slice.data, slice.size);
            rtp_packet.payload_header_size = slice.size;
        }
        if (!has_media_payload) {
            return false;
        }
        packets_.push_back(rtp_packet);
        return true;
    }

    std::vector<RtspRtpSender::RtpPacketInfo> TakePackets() {
        return std::move(packets_);
    }

private:
    const MediaFrame &frame_;
    std::vector<RtspRtpSender::RtpPacketInfo> packets_;
};

namespace {

constexpr int64_t kRtcpSenderReportIntervalMs = 1000;
constexpr uint64_t kNtpUnixEpochOffsetSeconds = 2208988800ULL;
constexpr size_t kRtpFrameSize = 8;

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

void WriteRtpHeader(uint8_t payload_type, bool marker, uint16_t sequence,
                    uint32_t timestamp, uint32_t ssrc, uint8_t *header) {
    if (header == nullptr) {
        return;
    }
    header[0] = 0x80;
    header[1] =
        static_cast<uint8_t>((marker ? 0x80 : 0x00) | payload_type);
    byte_writer::WriteU16(header + 2, sequence);
    byte_writer::WriteU32(header + 4, timestamp);
    byte_writer::WriteU32(header + 8, ssrc);
}

void AppendNtpTimestamp(std::string *packet, int64_t system_time_ms) {
    if (packet == nullptr) {
        return;
    }
    if (system_time_ms < 0) {
        system_time_ms = 0;
    }
    const uint64_t unix_ms = static_cast<uint64_t>(system_time_ms);
    const uint32_t ntp_seconds = static_cast<uint32_t>(
        (unix_ms / 1000ULL) + kNtpUnixEpochOffsetSeconds);
    const uint32_t ntp_fraction = static_cast<uint32_t>(
        ((unix_ms % 1000ULL) << 32) / 1000ULL);
    byte_writer::AppendU32(packet, ntp_seconds);
    byte_writer::AppendU32(packet, ntp_fraction);
}

std::string BuildRtcpSenderReport(uint32_t ssrc, uint32_t rtp_timestamp,
                                  uint32_t packet_count,
                                  uint32_t octet_count,
                                  int64_t system_time_ms) {
    std::string packet;
    packet.reserve(28);
    byte_writer::AppendU8(&packet, 0x80);
    byte_writer::AppendU8(&packet, 200);
    byte_writer::AppendU16(&packet, 6);
    byte_writer::AppendU32(&packet, ssrc);
    AppendNtpTimestamp(&packet, system_time_ms);
    byte_writer::AppendU32(&packet, rtp_timestamp);
    byte_writer::AppendU32(&packet, packet_count);
    byte_writer::AppendU32(&packet, octet_count);
    return packet;
}

}  // namespace

RtspRtpSender::RtspRtpSender(uint32_t rtp_mtu_bytes)
    : packetizer_(rtp_mtu_bytes) {}

void RtspRtpSender::SendFrame(RtspSession &session,
                              const MediaFrame &frame,
                              const RtspRtpSendRefs &refs) {
    // 每个 RTSP session 必须从关键帧开始；如果 subscription 溢出后重新等待关键帧，
    // 非关键帧直接丢弃，避免客户端解码参考帧缺失。
    bool should_drop = false;
    {
        std::lock_guard<std::mutex> lock(refs.mutex);
        if (!session.keyframe_seen) {
            if (!IsKeyframe(frame)) {
                ++session.stats.dropped_frames;
                ++refs.service_stats.dropped_frames;
                should_drop = true;
            } else {
                session.keyframe_seen = true;
            }
        }
    }
    if (should_drop) {
        return;
    }

    const std::shared_ptr<const RtpFrame> rtp_frame =
        GetRtpFrame(frame);
    if (!rtp_frame || rtp_frame->packets.empty()) {
        return;
    }

    uint16_t sequence = 0;
    uint32_t ssrc = 0;
    {
        std::lock_guard<std::mutex> lock(refs.mutex);
        sequence = session.rtp_sequence;
        ssrc = session.ssrc;
    }

    const uint8_t *payload = MediaFramePayloadData(rtp_frame->frame);
    for (const RtpPacketInfo &rtp_packet : rtp_frame->packets) {
        if (payload == nullptr ||
            rtp_packet.payload_size == 0 ||
            rtp_packet.payload_offset > rtp_frame->payload_size ||
            rtp_packet.payload_size >
                rtp_frame->payload_size - rtp_packet.payload_offset) {
            break;
        }

        uint8_t rtp_header[rtp::kRtpHeaderSize] = {};
        WriteRtpHeader(rtp_packet.payload_type, rtp_packet.marker,
                       sequence, rtp_packet.timestamp, ssrc, rtp_header);
        ++sequence;

        rtp::RtpPacketView packet;
        packet.marker = rtp_packet.marker;
        packet.payload_type = rtp_packet.payload_type;
        packet.sequence = static_cast<uint16_t>(sequence - 1);
        packet.timestamp = rtp_packet.timestamp;
        packet.ssrc = ssrc;
        if (!packet.SetRtpHeader(rtp_header, sizeof(rtp_header)) ||
            !packet.SetPayloadHeader(rtp_packet.payload_header,
                                     rtp_packet.payload_header_size) ||
            !packet.SetPayload(payload + rtp_packet.payload_offset,
                               rtp_packet.payload_size) ||
            !SendRtpPacketView(session, rtp_frame->frame, packet, refs)) {
            break;
        }
    }
    std::lock_guard<std::mutex> lock(refs.mutex);
    session.rtp_sequence = sequence;
}

std::shared_ptr<const RtspRtpSender::RtpFrame>
RtspRtpSender::GetRtpFrame(const MediaFrame &frame) {
    {
        std::lock_guard<std::mutex> lock(rtp_frame_mutex_);
        std::shared_ptr<const RtpFrame> cached =
            FindRtpFrameLocked(frame);
        if (cached) {
            return cached;
        }
    }

    auto rtp_frame = std::make_shared<RtpFrame>();
    rtp_frame->stream_id = frame.stream_id;
    rtp_frame->codec = frame.codec;
    rtp_frame->frame_sequence = frame.sequence;
    rtp_frame->pts_us = frame.pts_us;
    rtp_frame->dts_us = frame.dts_us;
    rtp_frame->payload_size = frame.payload.Size();
    rtp_frame->frame = frame;

    uint16_t ignored_sequence = 1;
    rtp::RtpPacketizerInput input;
    if (!BuildRtpInput(frame, ignored_sequence, 1, input)) {
        return std::shared_ptr<const RtpFrame>();
    }
    RtpFrameBuilder builder(frame);
    if (!packetizer_.Packetize(input, &builder)) {
        return std::shared_ptr<const RtpFrame>();
    }
    rtp_frame->packets = builder.TakePackets();
    if (rtp_frame->packets.empty()) {
        return std::shared_ptr<const RtpFrame>();
    }

    std::lock_guard<std::mutex> lock(rtp_frame_mutex_);
    std::shared_ptr<const RtpFrame> cached =
        FindRtpFrameLocked(frame);
    if (cached) {
        return cached;
    }
    AddRtpFrameLocked(rtp_frame);
    return rtp_frame;
}

std::shared_ptr<const RtspRtpSender::RtpFrame>
RtspRtpSender::FindRtpFrameLocked(const MediaFrame &frame) const {
    const std::deque<std::shared_ptr<const RtpFrame>> &cache =
        frame.stream_id == StreamId::kMain ? main_rtp_frames_
                                           : sub_rtp_frames_;
    for (const auto &rtp_frame : cache) {
        if (rtp_frame && IsSameRtpFrame(*rtp_frame, frame)) {
            return rtp_frame;
        }
    }
    return std::shared_ptr<const RtpFrame>();
}

void RtspRtpSender::AddRtpFrameLocked(
    const std::shared_ptr<const RtpFrame> &rtp_frame) {
    if (!rtp_frame) {
        return;
    }
    std::deque<std::shared_ptr<const RtpFrame>> &cache =
        rtp_frame->stream_id == StreamId::kMain ? main_rtp_frames_
                                                : sub_rtp_frames_;
    cache.push_back(rtp_frame);
    while (cache.size() > kRtpFrameSize) {
        cache.pop_front();
    }
}

bool RtspRtpSender::IsSameRtpFrame(const RtpFrame &rtp_frame,
                                   const MediaFrame &frame) {
    return rtp_frame.stream_id == frame.stream_id &&
           rtp_frame.codec == frame.codec &&
           rtp_frame.frame_sequence == frame.sequence &&
           rtp_frame.pts_us == frame.pts_us &&
           rtp_frame.dts_us == frame.dts_us &&
           rtp_frame.payload_size == frame.payload.Size();
}

bool RtspRtpSender::SendRtpPacketView(
    RtspSession &session,
    const MediaFrame &frame,
    const rtp::RtpPacketView &packet,
    const RtspRtpSendRefs &refs) {
    RtspRtpRoute route;
    {
        std::lock_guard<std::mutex> lock(refs.mutex);
        // 发送前复制 transport 快照，避免持锁调用 socket_io。SETUP/TEARDOWN 同时发生时，
        // 发送失败会走统一关闭路径。
        route.mode = session.transport;
        route.connection_id = session.connection_id;
        route.udp_socket_id = session.rtp_socket_id;
        route.udp_rtcp_socket_id = session.rtcp_socket_id;
        route.udp_peer = session.udp_rtp_peer;
        route.udp_rtcp_peer = session.udp_rtcp_peer;
        route.interleaved_rtp_channel = session.interleaved_rtp_channel;
    }

    const size_t packet_size = packet.Size();
    if (packet_size == 0 || packet_size > 0xffff) {
        return false;
    }
    const bool sent = RtspTransport::SendRtpPacket(
        refs.socket_io, route, frame, packet);
    if (!sent) {
        {
            std::lock_guard<std::mutex> lock(refs.mutex);
            ++session.stats.dropped_frames;
            ++refs.service_stats.dropped_frames;
        }
        if (route.mode == RtspTransportMode::kUdp) {
            return false;
        }
        // TCP interleaved 发送失败通常表示队列满或连接关闭，按慢客户端关闭控制连接。
        {
            std::lock_guard<std::mutex> lock(refs.mutex);
            ++refs.service_stats.slow_client_closes;
        }
        (void)refs.socket_io.Close(route.connection_id);
        return false;
    }

    bool send_sender_report = false;
    uint32_t sender_ssrc = 0;
    uint32_t sender_packet_count = 0;
    uint32_t sender_octet_count = 0;
    const int64_t now_ms = infra::Time::MonotonicMillis();
    {
        std::lock_guard<std::mutex> lock(refs.mutex);
        // pending_bytes 只对 TCP interleaved 有意义；UDP 下 socket_io 返回 0 或当前诊断值。
        ++session.stats.sent_rtp_packets;
        session.stats.sent_rtp_bytes += packet_size;
        session.stats.pending_bytes =
            refs.socket_io.PendingBytes(route.connection_id);
        ++refs.service_stats.sent_rtp_packets;
        refs.service_stats.sent_rtp_bytes += packet_size;
        if (session.last_rtcp_sender_report_ms == 0 ||
            now_ms - session.last_rtcp_sender_report_ms >=
                kRtcpSenderReportIntervalMs) {
            session.last_rtcp_sender_report_ms = now_ms;
            send_sender_report = true;
            sender_ssrc = session.ssrc;
            sender_packet_count = static_cast<uint32_t>(
                session.stats.sent_rtp_packets & 0xffffffffU);
            uint64_t rtp_payload_bytes = session.stats.sent_rtp_bytes;
            const uint64_t rtp_header_bytes =
                session.stats.sent_rtp_packets * rtp::kRtpHeaderSize;
            if (rtp_payload_bytes >= rtp_header_bytes) {
                rtp_payload_bytes -= rtp_header_bytes;
            }
            sender_octet_count = static_cast<uint32_t>(
                rtp_payload_bytes & 0xffffffffU);
        }
    }
    if (send_sender_report) {
        const std::string report = BuildRtcpSenderReport(
            sender_ssrc, packet.timestamp, sender_packet_count,
            sender_octet_count, infra::Time::SystemTimeMillis());
        (void)RtspTransport::SendRtcpSenderReport(
            refs.socket_io, route,
            reinterpret_cast<const uint8_t *>(report.data()),
            report.size());
    }
    return true;
}

}  // namespace live_stream
