#ifndef LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_SDP_H_
#define LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_SDP_H_

#include "webrtc.h"

#include <string>

namespace live_stream {
namespace webrtc_internal {

bool IsValidIceServerUrl(const std::string& url);
std::string BuildCandidateJson(const WebrtcIceCandidate& candidate);
std::string ReplaceHostCandidateIp(const std::string& candidate,
                                   const std::string& public_ip);

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_SDP_H_
