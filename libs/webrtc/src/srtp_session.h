#ifndef LIVE_STREAM_WEBRTC_SRC_SRTP_SESSION_H_
#define LIVE_STREAM_WEBRTC_SRC_SRTP_SESSION_H_

#include "media_mux.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

struct srtp_ctx_t_;

namespace live_stream {
namespace webrtc_internal {

enum class SrtpDirection {
  kInbound = 0,
  kOutbound,
};

enum class SrtpCryptoSuite {
  kNone = 0,
  kAes128CmSha180,
  kAes128CmSha132,
  kAeadAes128Gcm,
  kAeadAes256Gcm,
};

enum class RtcpFeedbackType {
  kNone = 0,
  kPli,
  kFir,
  kNack,
  kTransportCc,
};

struct RtcpFeedback {
  RtcpFeedbackType type = RtcpFeedbackType::kNone;
  uint32_t sender_ssrc = 0;
  uint32_t media_ssrc = 0;
};

class SrtpSession {
 public:
  SrtpSession() = default;
  ~SrtpSession();

  SrtpSession(const SrtpSession &) = delete;
  SrtpSession &operator=(const SrtpSession &) = delete;
  SrtpSession(SrtpSession &&other) noexcept;
  SrtpSession &operator=(SrtpSession &&other) noexcept;

  bool Start(SrtpDirection direction, SrtpCryptoSuite suite,
             const std::vector<uint8_t> &master_key);
  void Close();

  bool ProtectRtp(const media_mux::RtpPacketView &packet,
                  std::vector<uint8_t> *protected_packet);
  bool ProtectRtcp(const uint8_t *data, size_t size,
                   std::vector<uint8_t> *protected_packet);
  bool UnprotectRtcp(const uint8_t *data, size_t size,
                     std::vector<uint8_t> *plain_packet);

  static bool Available();
  static bool ParseRtcpFeedback(const uint8_t *data, size_t size,
                                RtcpFeedback *feedback);
  static bool IsKeyFrameRequest(RtcpFeedbackType type);

  bool ready() const { return session_ != nullptr; }

 private:
  srtp_ctx_t_ *session_ = nullptr;
  std::vector<uint8_t> master_key_;
};

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_SRTP_SESSION_H_
