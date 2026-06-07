#include "webrtc_sdp.h"

#include "config_json.h"
#include "dtls_transport.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <vector>

namespace live_stream {
namespace webrtc_internal {
namespace {

bool StartsWith(const std::string& text, const char* prefix) {
    const std::string expected(prefix);
    return text.compare(0, expected.size(), expected) == 0;
}

std::string Trim(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string ToLowerAscii(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (char ch : text) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

std::vector<std::string> SplitTokens(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

bool ParseIntToken(const std::string& token, int *value) {
    if (value == nullptr || token.empty()) {
        return false;
    }
    char *end = nullptr;
    const long parsed = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0' || parsed < 0 ||
        parsed > 127) {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

bool ParseUint32Token(const std::string& token, uint32_t *value) {
    if (value == nullptr || token.empty()) {
        return false;
    }
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0' ||
        parsed > 0xffffffffUL) {
        return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
}

size_t FindWhitespace(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (std::isspace(static_cast<unsigned char>(text[i])) != 0) {
            return i;
        }
    }
    return std::string::npos;
}

bool ContainsPayloadType(const std::vector<int>& payload_types,
                         int payload_type) {
    return std::find(payload_types.begin(), payload_types.end(),
                     payload_type) != payload_types.end();
}

bool ParseFingerprintValue(const std::string& value,
                           std::string *fingerprint_hash,
                           std::string *fingerprint) {
    if (fingerprint_hash == nullptr || fingerprint == nullptr) {
        return false;
    }
    const std::string trimmed = Trim(value);
    const size_t separator = FindWhitespace(trimmed);
    if (separator == std::string::npos) {
        return false;
    }
    const std::string hash = ToLowerAscii(Trim(trimmed.substr(0, separator)));
    const DtlsFingerprintAlgorithm algorithm =
        DtlsFingerprintAlgorithmFromString(hash);
    const std::string normalized =
        NormalizeDtlsFingerprintValue(Trim(trimmed.substr(separator + 1)));
    if (algorithm == DtlsFingerprintAlgorithm::kNone || normalized.empty()) {
        return false;
    }
    *fingerprint_hash = DtlsFingerprintAlgorithmName(algorithm);
    *fingerprint = normalized;
    return true;
}

bool ParseMediaPayloadTypes(const std::string& media_line,
                            std::vector<int> *payload_types) {
    if (payload_types == nullptr) {
        return false;
    }
    const std::vector<std::string> tokens = SplitTokens(media_line);
    if (tokens.size() < 4 || ToLowerAscii(tokens[0]) != "video") {
        return false;
    }
    payload_types->clear();
    for (size_t i = 3; i < tokens.size(); ++i) {
        int payload_type = -1;
        if (!ParseIntToken(tokens[i], &payload_type)) {
            return false;
        }
        payload_types->push_back(payload_type);
    }
    return !payload_types->empty();
}

bool ParseCodecName(const std::string& name, VideoCodec *codec) {
    if (codec == nullptr) {
        return false;
    }
    const std::string lower_name = ToLowerAscii(name);
    if (lower_name == "h264") {
        *codec = VideoCodec::kH264;
        return true;
    }
    if (lower_name == "h265" || lower_name == "hevc") {
        *codec = VideoCodec::kH265;
        return true;
    }
    return false;
}

bool ParseRtpmapValue(const std::string& value,
                      WebrtcSdpVideoCodec *codec) {
    if (codec == nullptr) {
        return false;
    }
    const std::vector<std::string> tokens = SplitTokens(value);
    if (tokens.size() < 2 || !ParseIntToken(tokens[0], &codec->payload_type)) {
        return false;
    }
    const std::string encoding = tokens[1];
    const size_t first_separator = encoding.find('/');
    if (first_separator == std::string::npos) {
        return false;
    }
    const size_t second_separator = encoding.find('/', first_separator + 1);
    const std::string codec_name = encoding.substr(0, first_separator);
    const std::string clock_text =
        second_separator == std::string::npos
            ? encoding.substr(first_separator + 1)
            : encoding.substr(first_separator + 1,
                              second_separator - first_separator - 1);
    if (!ParseCodecName(codec_name, &codec->codec) ||
        !ParseUint32Token(clock_text, &codec->clock_rate)) {
        return false;
    }
    return true;
}

bool ParsePayloadAttributeValue(const std::string& value, int *payload_type,
                                std::string *attribute) {
    if (payload_type == nullptr || attribute == nullptr) {
        return false;
    }
    const std::string trimmed = Trim(value);
    const size_t separator = FindWhitespace(trimmed);
    if (separator == std::string::npos) {
        return false;
    }
    if (!ParseIntToken(trimmed.substr(0, separator), payload_type)) {
        return false;
    }
    *attribute = Trim(trimmed.substr(separator + 1));
    return !attribute->empty();
}

bool ParseRtcpFeedbackValue(const std::string& value, int *payload_type,
                            std::string *feedback) {
    if (payload_type == nullptr || feedback == nullptr) {
        return false;
    }
    const std::string trimmed = Trim(value);
    const size_t separator = FindWhitespace(trimmed);
    if (separator == std::string::npos) {
        return false;
    }
    const std::string payload = trimmed.substr(0, separator);
    if (payload == "*") {
        *payload_type = -1;
    } else if (!ParseIntToken(payload, payload_type)) {
        return false;
    }
    *feedback = Trim(trimmed.substr(separator + 1));
    return !feedback->empty();
}

void AddUniqueFeedback(const std::string& feedback,
                       WebrtcSdpVideoCodec *codec) {
    if (codec == nullptr || feedback.empty()) {
        return;
    }
    if (std::find(codec->rtcp_feedback.begin(), codec->rtcp_feedback.end(),
                  feedback) == codec->rtcp_feedback.end()) {
        codec->rtcp_feedback.push_back(feedback);
    }
}

bool IsSupportedRtcpFeedback(const std::string& feedback,
                             std::string *normalized_feedback) {
    if (normalized_feedback == nullptr) {
        return false;
    }
    const std::string normalized = ToLowerAscii(Trim(feedback));
    if (normalized == "nack pli" || normalized == "ccm fir") {
        *normalized_feedback = normalized;
        return true;
    }
    return false;
}

void AddSupportedFeedback(const std::string& feedback,
                          WebrtcSdpVideoCodec *codec) {
    std::string normalized_feedback;
    if (IsSupportedRtcpFeedback(feedback, &normalized_feedback)) {
        AddUniqueFeedback(normalized_feedback, codec);
    }
}

bool HasPacketizationModeOne(const std::string& fmtp) {
    return ToLowerAscii(fmtp).find("packetization-mode=1") !=
           std::string::npos;
}

std::string BuildLocalH264Fmtp(const std::string& offer_fmtp) {
    if (!HasPacketizationModeOne(offer_fmtp)) {
        return std::string();
    }
    return "packetization-mode=1";
}

std::string BuildLocalCodecFmtp(VideoCodec codec,
                                const std::string& offer_fmtp) {
    if (codec == VideoCodec::kH264) {
        return BuildLocalH264Fmtp(offer_fmtp);
    }
    if (codec == VideoCodec::kH265) {
        return offer_fmtp;
    }
    return std::string();
}

bool SelectVideoCodec(const std::vector<int>& payload_types,
                      const std::map<int, WebrtcSdpVideoCodec>& codecs,
                      const std::map<int, std::string>& fmtps,
                      const std::map<int, std::vector<std::string>>& feedback,
                      VideoCodec local_codec,
                      WebrtcSdpVideoCodec *selected_codec) {
    if (selected_codec == nullptr) {
        return false;
    }

    for (int payload_type : payload_types) {
        auto codec_iter = codecs.find(payload_type);
        if (codec_iter == codecs.end() ||
            codec_iter->second.codec != local_codec ||
            codec_iter->second.clock_rate != 90000) {
            continue;
        }

        WebrtcSdpVideoCodec candidate = codec_iter->second;
        auto fmtp_iter = fmtps.find(payload_type);
        if (fmtp_iter != fmtps.end()) {
            candidate.fmtp = fmtp_iter->second;
        }
        candidate.fmtp = BuildLocalCodecFmtp(local_codec, candidate.fmtp);
        if (local_codec == VideoCodec::kH264 && candidate.fmtp.empty()) {
            continue;
        }
        auto wildcard_feedback = feedback.find(-1);
        if (wildcard_feedback != feedback.end()) {
            for (const std::string& item : wildcard_feedback->second) {
                AddSupportedFeedback(item, &candidate);
            }
        }
        auto feedback_iter = feedback.find(payload_type);
        if (feedback_iter != feedback.end()) {
            for (const std::string& item : feedback_iter->second) {
                AddSupportedFeedback(item, &candidate);
            }
        }

        *selected_codec = candidate;
        return true;
    }
    return false;
}

std::string SafeSdpToken(const std::string& token,
                         const std::string& fallback) {
    if (token.empty()) {
        return fallback;
    }
    for (char ch : token) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            return fallback;
        }
    }
    return token;
}

std::string AnswerSetupRole(const std::string& offer_setup) {
    const std::string setup = ToLowerAscii(offer_setup);
    if (setup == "actpass" || setup == "active") {
        return "passive";
    }
    return std::string();
}

const char *CodecRtpmapName(VideoCodec codec) {
    if (codec == VideoCodec::kH264) {
        return "H264";
    }
    if (codec == VideoCodec::kH265) {
        return "H265";
    }
    return nullptr;
}

}  // namespace

bool IsValidIceServerUrl(const std::string& url) {
    if (url.empty()) {
        return false;
    }
    return StartsWith(url, "stun:") || StartsWith(url, "stun://") ||
           StartsWith(url, "turn:") || StartsWith(url, "turn://");
}

std::string BuildCandidateJson(const WebrtcIceCandidate& candidate) {
    ConfigJson root = ConfigJson::object();
    root["candidate"] = candidate.candidate;
    root["sdpMid"] = candidate.sdp_mid;
    root["sdpMLineIndex"] = candidate.sdp_mline_index;
    if (!candidate.username_fragment.empty()) {
        root["usernameFragment"] = candidate.username_fragment;
    }
    return root.dump();
}

std::string ReplaceHostCandidateIp(const std::string& candidate,
                                   const std::string& public_ip) {
    if (candidate.empty() || public_ip.empty() ||
        candidate.find(" typ relay") != std::string::npos) {
        return candidate;
    }

    size_t cursor = candidate.find("candidate:");
    if (cursor == std::string::npos) {
        return candidate;
    }
    int spaces = 0;
    while (cursor < candidate.size() && spaces < 4) {
        if (candidate[cursor] == ' ') {
            ++spaces;
        }
        ++cursor;
    }
    if (spaces < 4 || cursor >= candidate.size()) {
        return candidate;
    }

    const size_t ip_start = cursor;
    while (cursor < candidate.size() && candidate[cursor] != ' ') {
        ++cursor;
    }
    if (ip_start == cursor) {
        return candidate;
    }
    return candidate.substr(0, ip_start) + public_ip +
           candidate.substr(cursor);
}

uint32_t BuildWebrtcSsrc(const std::string& peer_id) {
    uint32_t value = 0x57454252U;
    for (char c : peer_id) {
        value = value * 33U + static_cast<uint8_t>(c);
    }
    return value == 0 ? 0x57454252U : value;
}

bool ParseWebrtcOffer(const std::string& offer_sdp, VideoCodec local_codec,
                      WebrtcSdpOffer *offer) {
    if (offer == nullptr || offer_sdp.empty()) {
        return false;
    }
    if (local_codec != VideoCodec::kH264 && local_codec != VideoCodec::kH265) {
        return false;
    }

    std::string session_ice_ufrag;
    std::string session_ice_pwd;
    std::string session_fingerprint_hash;
    std::string session_fingerprint;
    std::string session_setup;
    std::vector<int> video_payload_types;
    std::map<int, WebrtcSdpVideoCodec> video_codecs;
    std::map<int, std::string> video_fmtps;
    std::map<int, std::vector<std::string>> video_feedback;

    WebrtcSdpOffer parsed_offer;
    bool in_session_section = true;
    bool in_video_section = false;
    bool saw_video_section = false;

    size_t line_start = 0;
    while (line_start < offer_sdp.size()) {
        size_t line_end = offer_sdp.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = offer_sdp.size();
        }
        std::string line = offer_sdp.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        line = Trim(line);

        if (StartsWith(line, "m=")) {
            in_session_section = false;
            const std::vector<std::string> media_tokens =
                SplitTokens(line.substr(2));
            const bool is_video =
                !media_tokens.empty() &&
                ToLowerAscii(media_tokens[0]) == "video";
            in_video_section = is_video && !saw_video_section;
            if (in_video_section) {
                saw_video_section =
                    ParseMediaPayloadTypes(line.substr(2),
                                           &video_payload_types);
                if (!saw_video_section) {
                    return false;
                }
            }
        } else if (StartsWith(line, "a=ice-ufrag:")) {
            const std::string value = Trim(line.substr(12));
            if (in_video_section) {
                parsed_offer.ice_ufrag = value;
            } else if (in_session_section) {
                session_ice_ufrag = value;
            }
        } else if (StartsWith(line, "a=ice-pwd:")) {
            const std::string value = Trim(line.substr(10));
            if (in_video_section) {
                parsed_offer.ice_pwd = value;
            } else if (in_session_section) {
                session_ice_pwd = value;
            }
        } else if (StartsWith(line, "a=fingerprint:")) {
            std::string fingerprint_hash;
            std::string fingerprint;
            if (!ParseFingerprintValue(line.substr(14), &fingerprint_hash,
                                       &fingerprint)) {
                return false;
            }
            if (in_video_section) {
                parsed_offer.fingerprint_hash = fingerprint_hash;
                parsed_offer.fingerprint = fingerprint;
            } else if (in_session_section) {
                session_fingerprint_hash = fingerprint_hash;
                session_fingerprint = fingerprint;
            }
        } else if (StartsWith(line, "a=setup:")) {
            const std::string value = ToLowerAscii(Trim(line.substr(8)));
            if (in_video_section) {
                parsed_offer.setup = value;
            } else if (in_session_section) {
                session_setup = value;
            }
        } else if (in_video_section && StartsWith(line, "a=mid:")) {
            const std::string value = Trim(line.substr(6));
            if (!value.empty()) {
                parsed_offer.video_mid = value;
                parsed_offer.has_video_mid = true;
            }
        } else if (in_video_section && line == "a=rtcp-mux") {
            parsed_offer.rtcp_mux = true;
        } else if (in_video_section && line == "a=rtcp-rsize") {
            parsed_offer.rtcp_rsize = true;
        } else if (in_video_section && StartsWith(line, "a=candidate:")) {
            parsed_offer.candidates.push_back(line.substr(2));
        } else if (in_video_section && StartsWith(line, "a=rtpmap:")) {
            WebrtcSdpVideoCodec codec;
            if (ParseRtpmapValue(line.substr(9), &codec) &&
                ContainsPayloadType(video_payload_types, codec.payload_type)) {
                video_codecs[codec.payload_type] = codec;
            }
        } else if (in_video_section && StartsWith(line, "a=fmtp:")) {
            int payload_type = -1;
            std::string fmtp;
            if (ParsePayloadAttributeValue(line.substr(7), &payload_type,
                                           &fmtp) &&
                ContainsPayloadType(video_payload_types, payload_type)) {
                video_fmtps[payload_type] = fmtp;
            }
        } else if (in_video_section && StartsWith(line, "a=rtcp-fb:")) {
            int payload_type = -1;
            std::string feedback;
            if (ParseRtcpFeedbackValue(line.substr(10), &payload_type,
                                       &feedback) &&
                (payload_type == -1 ||
                 ContainsPayloadType(video_payload_types, payload_type))) {
                video_feedback[payload_type].push_back(feedback);
            }
        }

        line_start = line_end + 1;
    }

    if (parsed_offer.ice_ufrag.empty()) {
        parsed_offer.ice_ufrag = session_ice_ufrag;
    }
    if (parsed_offer.ice_pwd.empty()) {
        parsed_offer.ice_pwd = session_ice_pwd;
    }
    if (parsed_offer.fingerprint.empty()) {
        parsed_offer.fingerprint_hash = session_fingerprint_hash;
        parsed_offer.fingerprint = session_fingerprint;
    }
    if (parsed_offer.setup.empty()) {
        parsed_offer.setup = session_setup;
    }
    if (!saw_video_section || parsed_offer.ice_ufrag.empty() ||
        parsed_offer.ice_pwd.empty() || parsed_offer.fingerprint.empty() ||
        parsed_offer.setup.empty() || !parsed_offer.has_video_mid ||
        !parsed_offer.rtcp_mux) {
        return false;
    }
    if (!SelectVideoCodec(video_payload_types, video_codecs, video_fmtps,
                          video_feedback, local_codec,
                          &parsed_offer.video_codec)) {
        return false;
    }

    *offer = parsed_offer;
    return true;
}

std::string BuildWebrtcAnswer(const WebrtcSdpOffer& offer,
                              const WebrtcSdpAnswerOptions& options) {
    if ((options.local_codec != VideoCodec::kH264 &&
         options.local_codec != VideoCodec::kH265) ||
        offer.video_codec.codec != options.local_codec ||
        offer.video_codec.payload_type < 0 ||
        offer.video_codec.clock_rate != 90000 || !offer.rtcp_mux ||
        options.local_port == 0 || options.local_ice_ufrag.empty() ||
        options.local_ice_pwd.empty() || options.local_fingerprint.empty()) {
        return std::string();
    }

    const std::string setup = AnswerSetupRole(offer.setup);
    if (setup.empty()) {
        return std::string();
    }

    std::string local_ip = options.local_ip;
    if (local_ip.empty() || local_ip == "0.0.0.0") {
        local_ip = options.local_candidate_ip;
    }
    if (local_ip.empty() || local_ip == "0.0.0.0") {
        return std::string();
    }
    const std::string mid = SafeSdpToken(offer.video_mid, std::string());
    if (mid.empty()) {
        return std::string();
    }
    const std::string peer_id = SafeSdpToken(options.peer_id, "live_stream");
    const uint32_t ssrc =
        options.local_ssrc == 0 ? BuildWebrtcSsrc(peer_id)
                                : options.local_ssrc;
    const std::string fingerprint_hash =
        options.local_fingerprint_hash.empty() ? "sha-256"
                                               : options.local_fingerprint_hash;
    const char *rtpmap_codec = CodecRtpmapName(options.local_codec);
    if (rtpmap_codec == nullptr) {
        return std::string();
    }

    std::ostringstream sdp;
    sdp << "v=0\r\n";
    sdp << "o=- 0 0 IN IP4 " << local_ip << "\r\n";
    sdp << "s=live_stream\r\n";
    sdp << "t=0 0\r\n";
    sdp << "a=group:BUNDLE " << mid << "\r\n";
    sdp << "a=msid-semantic: WMS live_stream\r\n";
    sdp << "m=video " << options.local_port
        << " UDP/TLS/RTP/SAVPF " << offer.video_codec.payload_type << "\r\n";
    sdp << "c=IN IP4 " << local_ip << "\r\n";
    sdp << "a=mid:" << mid << "\r\n";
    sdp << "a=sendonly\r\n";
    sdp << "a=rtcp-mux\r\n";
    if (offer.rtcp_rsize) {
        sdp << "a=rtcp-rsize\r\n";
    }
    sdp << "a=ice-ufrag:" << options.local_ice_ufrag << "\r\n";
    sdp << "a=ice-pwd:" << options.local_ice_pwd << "\r\n";
    sdp << "a=fingerprint:" << fingerprint_hash << " "
        << options.local_fingerprint << "\r\n";
    sdp << "a=setup:" << setup << "\r\n";
    sdp << "a=rtpmap:" << offer.video_codec.payload_type << " "
        << rtpmap_codec << "/90000\r\n";
    if (!offer.video_codec.fmtp.empty()) {
        sdp << "a=fmtp:" << offer.video_codec.payload_type << " "
            << offer.video_codec.fmtp << "\r\n";
    }
    for (const std::string& feedback : offer.video_codec.rtcp_feedback) {
        sdp << "a=rtcp-fb:" << offer.video_codec.payload_type << " "
            << feedback << "\r\n";
    }
    sdp << "a=msid:live_stream " << peer_id << "\r\n";
    sdp << "a=ssrc:" << ssrc << " cname:live_stream\r\n";
    sdp << "a=ssrc:" << ssrc << " msid:live_stream " << peer_id << "\r\n";
    sdp << "a=candidate:1 1 udp 2130706431 " << local_ip << " "
        << options.local_port << " typ host generation 0\r\n";
    sdp << "a=end-of-candidates\r\n";
    return sdp.str();
}

bool ParseRemoteFingerprint(const std::string& sdp,
                            DtlsFingerprint *fingerprint) {
    if (fingerprint == nullptr || sdp.empty()) {
        return false;
    }

    WebrtcSdpOffer offer;
    if (!ParseWebrtcOffer(sdp, VideoCodec::kH264, &offer) &&
        !ParseWebrtcOffer(sdp, VideoCodec::kH265, &offer)) {
        return false;
    }
    fingerprint->algorithm =
        DtlsFingerprintAlgorithmFromString(offer.fingerprint_hash);
    fingerprint->value = offer.fingerprint;
    return fingerprint->algorithm != DtlsFingerprintAlgorithm::kNone &&
           !fingerprint->value.empty();
}

}  // namespace webrtc_internal
}  // namespace live_stream
