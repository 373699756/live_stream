#ifndef LIVE_STREAM_WEBRTC_SRC_STUN_PACKET_H_
#define LIVE_STREAM_WEBRTC_SRC_STUN_PACKET_H_

#include "net.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace webrtc_internal {

enum class StunParseResult {
    kOk = 0,
    kNotStun,
    kUnsupported,
    kMalformed,
    kBadUsername,
    kBadMessageIntegrity,
    kBadFingerprint,
};

struct StunBindingRequest {
    std::array<uint8_t, 12> transaction_id{};
    std::string username;
    uint32_t priority = 0;
    bool has_message_integrity = false;
    bool has_fingerprint = false;
    bool use_candidate = false;
};

bool IsStunPacket(const uint8_t *data, size_t size);
StunParseResult ParseStunBindingRequest(const uint8_t *data, size_t size,
                                        const std::string &local_ufrag,
                                        const std::string &local_password,
                                        StunBindingRequest *request);
std::vector<uint8_t> BuildStunBindingSuccessResponse(
    const StunBindingRequest &request, const std::string &local_password,
    const NetAddress &peer);
const char *StunParseResultName(StunParseResult result);

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_STUN_PACKET_H_
