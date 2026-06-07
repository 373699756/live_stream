#ifndef LIVE_STREAM_WEBRTC_SRC_WEBRTC_SDP_H_
#define LIVE_STREAM_WEBRTC_SRC_WEBRTC_SDP_H_

#include "webrtc.h"

#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace webrtc_internal {

struct DtlsFingerprint;

struct WebrtcSdpVideoCodec {
    int payload_type = -1;
    VideoCodec codec = VideoCodec::kH264;
    uint32_t clock_rate = 0;
    std::string fmtp;
    std::vector<std::string> rtcp_feedback;
};

struct WebrtcSdpOffer {
    std::string video_mid = "0";
    bool has_video_mid = false;
    std::string ice_ufrag;
    std::string ice_pwd;
    std::string fingerprint_hash;
    std::string fingerprint;
    std::string setup;
    bool rtcp_mux = false;
    bool rtcp_rsize = false;
    std::vector<std::string> candidates;
    WebrtcSdpVideoCodec video_codec;
};

struct WebrtcSdpAnswerOptions {
    std::string peer_id;
    VideoCodec local_codec = VideoCodec::kH264;
    std::string local_ip = "127.0.0.1";
    std::string local_candidate_ip;
    uint16_t local_port = 0;
    std::string local_ice_ufrag;
    std::string local_ice_pwd;
    std::string local_fingerprint_hash = "sha-256";
    std::string local_fingerprint;
    uint32_t local_ssrc = 0;
};

bool IsValidIceServerUrl(const std::string& url);
std::string BuildCandidateJson(const WebrtcIceCandidate& candidate);
std::string ReplaceHostCandidateIp(const std::string& candidate,
                                   const std::string& public_ip);
uint32_t BuildWebrtcSsrc(const std::string& peer_id);
bool ParseWebrtcOffer(const std::string& offer_sdp, WebrtcSdpOffer *offer);
std::string BuildWebrtcAnswer(const WebrtcSdpOffer& offer,
                              const WebrtcSdpAnswerOptions& options);
bool ParseRemoteFingerprint(const std::string& sdp,
                            DtlsFingerprint *fingerprint);

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_WEBRTC_SDP_H_
