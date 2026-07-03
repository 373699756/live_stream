#include "rtsp_muxer.h"

#include "rtp.h"
#include "rtsp_protocol.h"

#include <cstdint>
#include <sstream>
#include <string>

namespace live_stream {
namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

struct H265ProfileTierLevel {
    uint32_t profile_id = 0;
    uint32_t tier_flag = 0;
    uint32_t level_id = 0;
    bool valid = false;
};

class BitReader {
public:
    explicit BitReader(const std::string &data) : data_(data) {}

    bool ReadBits(uint32_t count, uint32_t *value) {
        if (value == nullptr || count > 32) {
            return false;
        }
        uint32_t result = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (bit_offset_ >= data_.size() * 8) {
                return false;
            }
            const size_t byte_offset = bit_offset_ / 8;
            const uint32_t shift = 7 - static_cast<uint32_t>(bit_offset_ % 8);
            const uint8_t byte =
                static_cast<uint8_t>(static_cast<unsigned char>(
                    data_[byte_offset]));
            result = (result << 1) | ((byte >> shift) & 0x01);
            ++bit_offset_;
        }
        *value = result;
        return true;
    }

    bool SkipBits(uint32_t count) {
        uint32_t ignored = 0;
        while (count > 0) {
            const uint32_t step = count > 32 ? 32 : count;
            if (!ReadBits(step, &ignored)) {
                return false;
            }
            count -= step;
        }
        return true;
    }

private:
    const std::string &data_;
    size_t bit_offset_ = 0;
};

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

std::string EncodeBase64(const std::string &value) {
    std::string encoded;
    const size_t full_groups = value.size() / 3;
    encoded.reserve(((value.size() + 2) / 3) * 4);
    for (size_t i = 0; i < full_groups; ++i) {
        const size_t offset = i * 3;
        const uint32_t block =
            (static_cast<uint32_t>(
                 static_cast<unsigned char>(value[offset])) << 16) |
            (static_cast<uint32_t>(
                 static_cast<unsigned char>(value[offset + 1])) << 8) |
            static_cast<uint32_t>(
                static_cast<unsigned char>(value[offset + 2]));
        encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3f]);
        encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3f]);
        encoded.push_back(kBase64Alphabet[(block >> 6) & 0x3f]);
        encoded.push_back(kBase64Alphabet[block & 0x3f]);
    }

    const size_t remain = value.size() - full_groups * 3;
    if (remain == 1) {
        const uint32_t block =
            static_cast<uint32_t>(
                static_cast<unsigned char>(value[full_groups * 3]))
            << 16;
        encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3f]);
        encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3f]);
        encoded.append("==");
    } else if (remain == 2) {
        const size_t offset = full_groups * 3;
        const uint32_t block =
            (static_cast<uint32_t>(
                 static_cast<unsigned char>(value[offset])) << 16) |
            (static_cast<uint32_t>(
                 static_cast<unsigned char>(value[offset + 1])) << 8);
        encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3f]);
        encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3f]);
        encoded.push_back(kBase64Alphabet[(block >> 6) & 0x3f]);
        encoded.push_back('=');
    }
    return encoded;
}

std::string H265NalPayloadToRbsp(const std::string &nal) {
    std::string rbsp;
    if (nal.size() <= 2) {
        return rbsp;
    }
    rbsp.reserve(nal.size() - 2);
    for (size_t i = 2; i < nal.size(); ++i) {
        const uint8_t byte =
            static_cast<uint8_t>(static_cast<unsigned char>(nal[i]));
        if (i >= 4 && byte == 0x03 &&
            static_cast<uint8_t>(static_cast<unsigned char>(nal[i - 1])) ==
                0x00 &&
            static_cast<uint8_t>(static_cast<unsigned char>(nal[i - 2])) ==
                0x00) {
            continue;
        }
        rbsp.push_back(static_cast<char>(byte));
    }
    return rbsp;
}

H265ProfileTierLevel ParseH265ProfileTierLevel(const std::string &sps) {
    H265ProfileTierLevel profile_tier_level;
    const std::string rbsp = H265NalPayloadToRbsp(sps);
    if (rbsp.empty()) {
        return profile_tier_level;
    }

    BitReader reader(rbsp);
    uint32_t profile_id = 0;
    uint32_t tier_flag = 0;
    uint32_t level_id = 0;
    if (!reader.SkipBits(4) ||  // sps_video_parameter_set_id
        !reader.SkipBits(3) ||  // sps_max_sub_layers_minus1
        !reader.SkipBits(1) ||  // sps_temporal_id_nesting_flag
        !reader.SkipBits(2) ||  // general_profile_space
        !reader.ReadBits(1, &tier_flag) ||
        !reader.ReadBits(5, &profile_id) ||
        !reader.SkipBits(32) ||  // general_profile_compatibility_flags
        !reader.SkipBits(48) ||  // general_constraint_indicator_flags
        !reader.ReadBits(8, &level_id)) {
        return H265ProfileTierLevel();
    }
    profile_tier_level.profile_id = profile_id;
    profile_tier_level.tier_flag = tier_flag;
    profile_tier_level.level_id = level_id;
    profile_tier_level.valid = level_id != 0;
    return profile_tier_level;
}

void AppendCodecFmtp(const MediaStreamInfo &stream_info,
                     uint8_t payload_type,
                     std::ostringstream &sdp) {
    if (stream_info.codec == Codec::kH264 && !stream_info.sps.empty() &&
        !stream_info.pps.empty()) {
        sdp << "a=fmtp:" << static_cast<int>(payload_type)
            << " packetization-mode=1;sprop-parameter-sets="
            << EncodeBase64(stream_info.sps) << ","
            << EncodeBase64(stream_info.pps) << "\r\n";
        return;
    }
    if (stream_info.codec == Codec::kH265 && !stream_info.vps.empty() &&
        !stream_info.sps.empty() && !stream_info.pps.empty()) {
        const H265ProfileTierLevel profile_tier_level =
            ParseH265ProfileTierLevel(stream_info.sps);
        sdp << "a=fmtp:" << static_cast<int>(payload_type) << " ";
        if (profile_tier_level.valid) {
            sdp << "level-id=" << profile_tier_level.level_id
                << "; profile-id=" << profile_tier_level.profile_id
                << "; tier-flag=" << profile_tier_level.tier_flag << "; ";
        }
        sdp << "sprop-vps=" << EncodeBase64(stream_info.vps)
            << "; sprop-sps=" << EncodeBase64(stream_info.sps)
            << "; sprop-pps=" << EncodeBase64(stream_info.pps) << "\r\n";
    }
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
    AppendCodecFmtp(stream_info, payload_type, sdp);
    sdp << "a=control:trackID=0\r\n";
    return sdp.str();
}

}  // namespace live_stream
