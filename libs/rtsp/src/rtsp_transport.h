#ifndef LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_TRANSPORT_H_
#define LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_TRANSPORT_H_

#include "media/encoded_frame.h"
#include "net.h"
#include "rtsp.h"
#include "media_mux.h"

#include <cstddef>
#include <cstdint>

namespace live_stream {

struct RtspTransportTarget {
  RtspTransportMode mode = RtspTransportMode::kTcpInterleaved;
  ConnectionId connection_id = 0;
  UdpSocketId udp_socket_id = 0;
  NetAddress udp_peer;
  uint8_t interleaved_rtp_channel = 0;
};

class RtspTransport {
 public:
  static bool SendRtpPacket(NetEngine *net_engine,
                            const RtspTransportTarget &target,
                            const EncodedFrame &frame,
                            const media_mux::RtpPacketView &packet);
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_TRANSPORT_H_
