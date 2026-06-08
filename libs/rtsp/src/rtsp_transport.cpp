#include "rtsp_transport.h"

#include "byte_writer.h"
#include "media/media_buffer.h"

namespace live_stream {
namespace {

void RefVideoBufferOwner(const void *owner) {
  (void)VideoBufferRef(
      const_cast<VideoBuffer *>(static_cast<const VideoBuffer *>(owner)));
}

void UnrefVideoBufferOwner(const void *owner) {
  VideoBufferUnref(
      const_cast<VideoBuffer *>(static_cast<const VideoBuffer *>(owner)));
}

NetBufferOwner VideoBufferNetOwner(VideoBuffer *buffer) {
  if (buffer == nullptr) {
    return NetBufferOwner{};
  }
  return NetBufferOwner{buffer, RefVideoBufferOwner, UnrefVideoBufferOwner};
}

}  // namespace

bool RtspTransport::SendRtpPacket(
    INetEngine *net_engine, const RtspTransportTarget &target,
    const EncodedFrame &frame, const rtp::RtpPacketView &packet) {
  const size_t packet_size = packet.Size();
  if (net_engine == nullptr || packet_size == 0 || packet_size > 0xffff) {
    return false;
  }

  // TCP interleaved 异步排队，media payload 必须带 VideoBuffer owner；
  // UDP sendmsg 调用返回后不保留 slice，所以不需要 owner。
  const NetBufferOwner payload_owner = VideoBufferNetOwner(frame.buffer);
  NetBufferSlices slices;
  bool ok = true;
  uint8_t interleaved_header[4] = {
      '$', target.interleaved_rtp_channel, 0, 0};
  if (target.mode == RtspTransportMode::kTcpInterleaved) {
    // RTSP TCP interleaved 帧格式为 '$' + channel + 16bit length + RTP packet。
    // RTP packet 本身保持分片，交给 net 一次性排队，减少热路径复制。
    byte_writer::WriteU16(interleaved_header + 2,
                          static_cast<uint16_t>(packet_size));
    ok = slices.Add(interleaved_header, sizeof(interleaved_header));
    for (size_t i = 0; ok && i < packet.slice_count; ++i) {
      const rtp::RtpPacketSlice &slice = packet.slices[i];
      ok = slices.Add(slice.data, slice.size,
                      slice.media_payload ? payload_owner
                                          : NetBufferOwner{});
    }
    return ok && net_engine->SendSlices(target.connection_id, slices);
  }

  if (target.mode == RtspTransportMode::kUdp && target.udp_socket_id != 0) {
    // UDP RTP 是完整 datagram，超过 MTU 的分片已经由 RtpPacketizer 完成；
    // 这里只把 RTP header/payload slices 聚合给 sendmsg。
    for (size_t i = 0; ok && i < packet.slice_count; ++i) {
      const rtp::RtpPacketSlice &slice = packet.slices[i];
      ok = slices.Add(slice.data, slice.size);
    }
    return ok && net_engine->SendToSlices(target.udp_socket_id,
                                          target.udp_peer, slices);
  }

  return false;
}

}  // namespace live_stream
