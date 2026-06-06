#include "webrtc_sdp.h"

#include "config_json.h"

namespace live_stream {
namespace webrtc_internal {
namespace {

bool StartsWith(const std::string& text, const char* prefix) {
    const std::string expected(prefix);
    return text.compare(0, expected.size(), expected) == 0;
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

}  // namespace webrtc_internal
}  // namespace live_stream
