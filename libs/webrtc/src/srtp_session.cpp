#include "srtp_session.h"

#include <srtp2/srtp.h>

#include <cstring>
#include <mutex>
#include <utility>

namespace live_stream {
namespace webrtc_internal {
namespace {

constexpr size_t kRtcpCommonHeaderSize = 4;
constexpr size_t kRtcpFeedbackHeaderSize = 12;
constexpr uint8_t kRtcpPacketTypePayloadFeedback = 206;
constexpr uint8_t kRtcpPacketTypeRtpFeedback = 205;
constexpr uint8_t kRtcpFeedbackFormatPli = 1;
constexpr uint8_t kRtcpFeedbackFormatFir = 4;
constexpr uint8_t kRtcpFeedbackFormatNack = 1;
constexpr uint8_t kRtcpFeedbackFormatTransportCc = 15;
constexpr unsigned long kReplayWindowSize = 0x8000 - 1;

struct SrtpEnvironment {
    SrtpEnvironment() {
        ready = srtp_init() == srtp_err_status_ok;
    }

    ~SrtpEnvironment() {
        if (ready) {
            (void)srtp_shutdown();
        }
    }

    bool ready = false;
};

SrtpEnvironment &Environment() {
    static SrtpEnvironment environment;
    return environment;
}

bool EnsureSrtpReady() {
    return Environment().ready;
}

uint32_t ReadBigEndian32(const uint8_t *data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

void SetPolicyForSuite(SrtpCryptoSuite suite, srtp_policy_t *policy) {
    switch (suite) {
        case SrtpCryptoSuite::kAes128CmSha180:
            srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy->rtp);
            srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy->rtcp);
            break;
        case SrtpCryptoSuite::kAes128CmSha132:
            srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&policy->rtp);
            srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy->rtcp);
            break;
        case SrtpCryptoSuite::kAeadAes128Gcm:
            srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy->rtp);
            srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy->rtcp);
            break;
        case SrtpCryptoSuite::kAeadAes256Gcm:
            srtp_crypto_policy_set_aes_gcm_256_16_auth(&policy->rtp);
            srtp_crypto_policy_set_aes_gcm_256_16_auth(&policy->rtcp);
            break;
        case SrtpCryptoSuite::kNone:
            break;
    }
}

bool BuildPolicy(SrtpDirection direction, SrtpCryptoSuite suite,
                 std::vector<uint8_t> *master_key, srtp_policy_t *policy) {
    if (master_key == nullptr || policy == nullptr || master_key->empty() ||
        suite == SrtpCryptoSuite::kNone) {
        return false;
    }

    std::memset(policy, 0, sizeof(*policy));
    SetPolicyForSuite(suite, policy);
    if (policy->rtp.cipher_key_len <= 0 ||
        master_key->size() !=
            static_cast<size_t>(policy->rtp.cipher_key_len)) {
        return false;
    }

    policy->ssrc.type = direction == SrtpDirection::kOutbound
                            ? ssrc_any_outbound
                            : ssrc_any_inbound;
    policy->ssrc.value = 0;
    policy->key = master_key->data();
    policy->window_size = kReplayWindowSize;
    policy->allow_repeat_tx = 1;
    policy->next = nullptr;
    return true;
}

bool CopyRtpPacket(const rtp::RtpPacketView &packet,
                   std::vector<uint8_t> *buffer) {
    if (buffer == nullptr || packet.Size() == 0) {
        return false;
    }
    const size_t packet_size = packet.Size();
    // libsrtp 需要一块可原地追加认证尾部的连续 RTP buffer。这里会把 RTP
    // header、FU header 和媒体 payload slice 复制一次；这是 WebRTC/SRTP 路径
    // 为加密必需的拷贝，不会长期保存原始 EncodedFrame 指针。
    buffer->resize(packet_size + SRTP_MAX_TRAILER_LEN);
    size_t offset = 0;
    for (size_t i = 0; i < packet.slice_count; ++i) {
        const rtp::RtpPacketSlice &slice = packet.slices[i];
        if (slice.data == nullptr || slice.size == 0 ||
            offset > packet_size || slice.size > packet_size - offset) {
            return false;
        }
        std::memcpy(buffer->data() + offset, slice.data, slice.size);
        offset += slice.size;
    }
    if (offset != packet_size) {
        return false;
    }
    return true;
}

}  // namespace

SrtpSession::~SrtpSession() { Close(); }

SrtpSession::SrtpSession(SrtpSession &&other) noexcept
    : session_(other.session_), master_key_(std::move(other.master_key_)) {
    other.session_ = nullptr;
}

SrtpSession &SrtpSession::operator=(SrtpSession &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    Close();
    session_ = other.session_;
    master_key_ = std::move(other.master_key_);
    other.session_ = nullptr;
    return *this;
}

bool SrtpSession::Start(SrtpDirection direction, SrtpCryptoSuite suite,
                        const std::vector<uint8_t> &master_key) {
    Close();
    if (!EnsureSrtpReady()) {
        return false;
    }
    master_key_ = master_key;
    srtp_policy_t policy;
    if (!BuildPolicy(direction, suite, &master_key_, &policy)) {
        master_key_.clear();
        return false;
    }
    srtp_t session = nullptr;
    if (srtp_create(&session, &policy) != srtp_err_status_ok ||
        session == nullptr) {
        master_key_.clear();
        return false;
    }
    session_ = session;
    return true;
}

void SrtpSession::Close() {
    if (session_ != nullptr) {
        (void)srtp_dealloc(session_);
    }
    session_ = nullptr;
    master_key_.clear();
}

bool SrtpSession::ProtectRtp(
    const rtp::RtpPacketView &packet,
    std::vector<uint8_t> *protected_packet) {
    if (session_ == nullptr || protected_packet == nullptr ||
        !CopyRtpPacket(packet, protected_packet)) {
        return false;
    }

    int packet_size = static_cast<int>(packet.Size());
    if (srtp_protect(session_, protected_packet->data(), &packet_size) !=
            srtp_err_status_ok ||
        packet_size <= 0) {
        protected_packet->clear();
        return false;
    }
    protected_packet->resize(static_cast<size_t>(packet_size));
    return true;
}

bool SrtpSession::ProtectRtcp(const uint8_t *data, size_t size,
                              std::vector<uint8_t> *protected_packet) {
    if (session_ == nullptr || data == nullptr || size == 0 ||
        protected_packet == nullptr) {
        return false;
    }

    protected_packet->resize(size + SRTP_MAX_SRTCP_TRAILER_LEN);
    std::memcpy(protected_packet->data(), data, size);
    int packet_size = static_cast<int>(size);
    if (srtp_protect_rtcp(session_, protected_packet->data(), &packet_size) !=
            srtp_err_status_ok ||
        packet_size <= 0) {
        protected_packet->clear();
        return false;
    }
    protected_packet->resize(static_cast<size_t>(packet_size));
    return true;
}

bool SrtpSession::UnprotectRtcp(const uint8_t *data, size_t size,
                                std::vector<uint8_t> *plain_packet) {
    if (session_ == nullptr || data == nullptr || size == 0 ||
        plain_packet == nullptr) {
        return false;
    }

    plain_packet->resize(size);
    std::memcpy(plain_packet->data(), data, size);
    int packet_size = static_cast<int>(plain_packet->size());
    if (srtp_unprotect_rtcp(session_, plain_packet->data(), &packet_size) !=
            srtp_err_status_ok ||
        packet_size <= 0) {
        plain_packet->clear();
        return false;
    }
    plain_packet->resize(static_cast<size_t>(packet_size));
    return true;
}

bool SrtpSession::Available() {
    return EnsureSrtpReady();
}

bool SrtpSession::ParseRtcpFeedback(const uint8_t *data, size_t size,
                                    RtcpFeedback *feedback) {
    if (feedback == nullptr) {
        return false;
    }
    *feedback = RtcpFeedback();
    if (data == nullptr || size < kRtcpFeedbackHeaderSize) {
        return false;
    }

    size_t offset = 0;
    while (offset + kRtcpCommonHeaderSize <= size) {
        const uint8_t fmt = data[offset] & 0x1f;
        const uint8_t packet_type = data[offset + 1];
        const uint16_t length_words =
            (static_cast<uint16_t>(data[offset + 2]) << 8) |
            static_cast<uint16_t>(data[offset + 3]);
        const size_t packet_size =
            (static_cast<size_t>(length_words) + 1) * 4;
        if (packet_size < kRtcpCommonHeaderSize ||
            packet_size > size - offset) {
            return false;
        }

        if ((packet_type == kRtcpPacketTypePayloadFeedback ||
             packet_type == kRtcpPacketTypeRtpFeedback) &&
            packet_size >= kRtcpFeedbackHeaderSize) {
            feedback->sender_ssrc = ReadBigEndian32(data + offset + 4);
            feedback->media_ssrc = ReadBigEndian32(data + offset + 8);
            if (packet_type == kRtcpPacketTypePayloadFeedback &&
                fmt == kRtcpFeedbackFormatPli) {
                feedback->type = RtcpFeedbackType::kPli;
                return true;
            }
            if (packet_type == kRtcpPacketTypePayloadFeedback &&
                fmt == kRtcpFeedbackFormatFir) {
                feedback->type = RtcpFeedbackType::kFir;
                return true;
            }
            if (packet_type == kRtcpPacketTypeRtpFeedback &&
                fmt == kRtcpFeedbackFormatNack) {
                feedback->type = RtcpFeedbackType::kNack;
                return true;
            }
            if (packet_type == kRtcpPacketTypeRtpFeedback &&
                fmt == kRtcpFeedbackFormatTransportCc) {
                feedback->type = RtcpFeedbackType::kTransportCc;
                return true;
            }
        }
        offset += packet_size;
    }
    return false;
}

bool SrtpSession::CountRtcpFeedback(const uint8_t *data, size_t size,
                                    RtcpFeedbackCounters *counters) {
    if (counters == nullptr) {
        return false;
    }
    *counters = RtcpFeedbackCounters();
    if (data == nullptr || size < kRtcpCommonHeaderSize) {
        return false;
    }

    size_t offset = 0;
    while (offset + kRtcpCommonHeaderSize <= size) {
        const uint8_t fmt = data[offset] & 0x1f;
        const uint8_t packet_type = data[offset + 1];
        const uint16_t length_words =
            (static_cast<uint16_t>(data[offset + 2]) << 8) |
            static_cast<uint16_t>(data[offset + 3]);
        const size_t packet_size =
            (static_cast<size_t>(length_words) + 1) * 4;
        if (packet_size < kRtcpCommonHeaderSize ||
            packet_size > size - offset) {
            return false;
        }

        if ((packet_type == kRtcpPacketTypePayloadFeedback ||
             packet_type == kRtcpPacketTypeRtpFeedback) &&
            packet_size >= kRtcpFeedbackHeaderSize) {
            if (packet_type == kRtcpPacketTypePayloadFeedback &&
                fmt == kRtcpFeedbackFormatPli) {
                ++counters->pli_count;
            } else if (packet_type == kRtcpPacketTypePayloadFeedback &&
                       fmt == kRtcpFeedbackFormatFir) {
                ++counters->fir_count;
            } else if (packet_type == kRtcpPacketTypeRtpFeedback &&
                       fmt == kRtcpFeedbackFormatNack) {
                ++counters->nack_count;
            } else if (packet_type == kRtcpPacketTypeRtpFeedback &&
                       fmt == kRtcpFeedbackFormatTransportCc) {
                ++counters->transport_cc_count;
            }
        }
        offset += packet_size;
    }
    return offset == size;
}

bool SrtpSession::IsRtcpKeyframeRequest(RtcpFeedbackType type) {
    return type == RtcpFeedbackType::kPli || type == RtcpFeedbackType::kFir;
}

}  // namespace webrtc_internal
}  // namespace live_stream
