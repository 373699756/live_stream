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
    NetEngine *net_engine, const RtspTransportTarget &target,
    const EncodedFrame &frame, const media_mux::RtpPacketView &packet) {
  const size_t packet_size = packet.Size();
  if (net_engine == nullptr || packet_size == 0 || packet_size > 0xffff) {
    return false;
  }

  const NetBufferOwner payload_owner = VideoBufferNetOwner(frame.buffer);
  NetBufferSlices slices;
  bool ok = true;
  uint8_t interleaved_header[4] = {
      '$', target.interleaved_rtp_channel, 0, 0};
  if (target.mode == RtspTransportMode::kTcpInterleaved) {
    byte_writer::WriteU16(interleaved_header + 2,
                          static_cast<uint16_t>(packet_size));
    ok = slices.Add(interleaved_header, sizeof(interleaved_header));
    for (size_t i = 0; ok && i < packet.slice_count; ++i) {
      const media_mux::RtpPacketSlice &slice = packet.slices[i];
      ok = slices.Add(slice.data, slice.size,
                      slice.media_payload ? payload_owner
                                          : NetBufferOwner{});
    }
    return ok && net_engine->SendSlices(target.connection_id, slices);
  }

  if (target.mode == RtspTransportMode::kUdp && target.udp_socket_id != 0) {
    for (size_t i = 0; ok && i < packet.slice_count; ++i) {
      const media_mux::RtpPacketSlice &slice = packet.slices[i];
      ok = slices.Add(slice.data, slice.size);
    }
    return ok && net_engine->SendToSlices(target.udp_socket_id,
                                          target.udp_peer, slices);
  }

  return false;
}

}  // namespace live_stream
