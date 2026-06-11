#include "rtsp_muxer.h"

#include "rtp.h"
#include "rtsp_protocol.h"

#include <sstream>

namespace live_stream {
namespace {

const char *RtpEncodingName(Codec codec) {
    return codec == Codec::kH265 ? "H265" : "H264";
}

rtp::Codec RtpCodecFromCodec(Codec codec) {
    return codec == Codec::kH265 ? rtp::Codec::kH265
                                      : rtp::Codec::kH264;
}

}  // namespace

std::string RtspMuxer::BuildSdp(const RtspListenAddress &address,
                                StreamId stream_id,
                                const MediaStreamInfo &stream_info) {
    const uint8_t payload_type = rtp::RtpPayloadTypeForCodec(
        RtpCodecFromCodec(stream_info.codec));
    std::ostringstream sdp;
    sdp << "v=0\r\n";
    sdp << "o=- 0 0 IN IP4 " << address.ip << "\r\n";
    sdp << "s=live_stream\r\n";
    sdp << "c=IN IP4 0.0.0.0\r\n";
    sdp << "t=0 0\r\n";
    sdp << "a=control:" << rtsp_internal::StreamPath(stream_id)
        << "\r\n";
    sdp << "m=video 0 RTP/AVP " << static_cast<int>(payload_type) << "\r\n";
    sdp << "a=rtpmap:" << static_cast<int>(payload_type) << " "
        << RtpEncodingName(stream_info.codec) << "/"
        << (stream_info.clock_rate != 0 ? stream_info.clock_rate
                                        : rtp::kRtpClockRate)
        << "\r\n";
    sdp << "a=control:trackID=0\r\n";
    return sdp.str();
}

}  // namespace live_stream
