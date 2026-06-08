#include "rtsp_muxer.h"

#include "rtp.h"
#include "rtsp_protocol.h"

#include <sstream>

namespace live_stream {
namespace {

uint8_t PayloadType(VideoCodec codec) {
    return codec == VideoCodec::kH265 ? rtp::kRtpPayloadTypeH265
                                      : rtp::kRtpPayloadTypeH264;
}

const char *RtpEncodingName(VideoCodec codec) {
    return codec == VideoCodec::kH265 ? "H265" : "H264";
}

}  // namespace

std::string RtspMuxer::BuildSdp(const RtspListenAddress &address,
                                const MediaTrack &track) {
    const uint8_t payload_type = PayloadType(track.codec);
    std::ostringstream sdp;
    sdp << "v=0\r\n";
    sdp << "o=- 0 0 IN IP4 " << address.ip << "\r\n";
    sdp << "s=live_stream\r\n";
    sdp << "c=IN IP4 0.0.0.0\r\n";
    sdp << "t=0 0\r\n";
    sdp << "a=control:" << rtsp_internal::StreamPath(track.stream_id)
        << "\r\n";
    sdp << "m=video 0 RTP/AVP " << static_cast<int>(payload_type) << "\r\n";
    sdp << "a=rtpmap:" << static_cast<int>(payload_type) << " "
        << RtpEncodingName(track.codec) << "/"
        << (track.clock_rate != 0 ? track.clock_rate
                                  : rtp::kRtpClockRate)
        << "\r\n";
    sdp << "a=control:trackID=0\r\n";
    return sdp.str();
}

}  // namespace live_stream
