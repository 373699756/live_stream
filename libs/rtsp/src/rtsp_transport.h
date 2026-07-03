#ifndef LIVE_STREAM_RTSP_SRC_RTSP_TRANSPORT_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_TRANSPORT_H_

#include "media/media_frame.h"
#include "socket_io.h"
#include "rtsp.h"
#include "rtp.h"

#include <cstddef>
#include <cstdint>

namespace live_stream {

struct RtspRtpRoute {
    RtspTransportMode mode = RtspTransportMode::kTcpInterleaved;
    ConnectionId connection_id = 0;
    UdpSocketId udp_socket_id = 0;
    UdpSocketId udp_rtcp_socket_id = 0;
    SocketAddress udp_peer;
    SocketAddress udp_rtcp_peer;
    uint8_t interleaved_rtp_channel = 0;
};

class RtspTransport {
public:
    static bool SendRtpPacket(ISocketIo &socket_io,
                              const RtspRtpRoute &route,
                              const MediaFrame &frame,
                              const rtp::RtpPacketView &packet);
    static bool SendRtcpSenderReport(ISocketIo &socket_io,
                                     const RtspRtpRoute &route,
                                     const uint8_t *data,
                                     size_t size);
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_TRANSPORT_H_
