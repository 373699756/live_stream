#include "rtsp_muxer.h"

#include "rtp.h"
#include "rtsp_protocol.h"

#include <sstream>

namespace live_stream {
namespace {

const char *RtpEncodingName(Codec codec) {
    return codec == Codec::kH265 ? "H265" : "H264";
}

bool RtpCodecFromCodec(Codec codec, rtp::Codec *rtp_codec) {
    if (rtp_codec == nullptr) {
        return false;
    }
    if (codec == Codec::kH264) {
        *rtp_codec = rtp::Codec::kH264;
        return true;
    }
    if (codec == Codec::kH265) {
        *rtp_codec = rtp::Codec::kH265;
        return true;
    }
    return false;
}

}  // namespace

bool RtspMuxer::IsCodecSupported(Codec codec) {
    return codec == Codec::kH264 || codec == Codec::kH265;
}

std::string RtspMuxer::BuildSdp(const RtspListenAddress &address,
                                StreamId stream_id,
                                const MediaStreamInfo &stream_info) {
    rtp::Codec rtp_codec = rtp::Codec::kH264;
    if (!RtpCodecFromCodec(stream_info.codec, &rtp_codec)) {
        return std::string();
    }
    const uint8_t payload_type = rtp::RtpPayloadTypeForCodec(
        rtp_codec);
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
