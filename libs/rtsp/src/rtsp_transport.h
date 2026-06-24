#ifndef LIVE_STREAM_RTSP_SRC_RTSP_TRANSPORT_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_TRANSPORT_H_

#include "media/media_frame.h"
#include "net.h"
#include "rtsp.h"
#include "rtp.h"

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
    static bool SendRtpPacket(INetIo *net_io,
                              const RtspTransportTarget &target,
                              const MediaFrame &frame,
                              const rtp::RtpPacketView &packet);
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_TRANSPORT_H_
