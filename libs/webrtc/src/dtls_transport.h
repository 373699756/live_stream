#ifndef LIVE_STREAM_WEBRTC_SRC_DTLS_TRANSPORT_H_
#define LIVE_STREAM_WEBRTC_SRC_DTLS_TRANSPORT_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace webrtc_internal {

enum class DtlsState {
    kNew = 0,
    kConnecting,
    kConnected,
    kFailed,
    kClosed,
};

enum class DtlsFingerprintAlgorithm {
    kNone = 0,
    kSha1,
    kSha224,
    kSha256,
    kSha384,
    kSha512,
};

enum class DtlsSrtpCryptoSuite {
    kNone = 0,
    kAes128CmSha1_80,
};

struct DtlsFingerprint {
    DtlsFingerprintAlgorithm algorithm = DtlsFingerprintAlgorithm::kNone;
    std::string value;
};

struct DtlsSrtpKeys {
    DtlsSrtpCryptoSuite suite = DtlsSrtpCryptoSuite::kNone;
    std::vector<uint8_t> local_master_key;
    std::vector<uint8_t> remote_master_key;
};

struct DtlsProcessResult {
    DtlsState state = DtlsState::kNew;
    std::vector<uint8_t> outgoing_dtls;
    std::vector<uint8_t> application_data;
    DtlsSrtpKeys srtp_keys;
    std::string error;
};

const char *DtlsFingerprintAlgorithmName(DtlsFingerprintAlgorithm algorithm);
DtlsFingerprintAlgorithm DtlsFingerprintAlgorithmFromString(
    const std::string &algorithm);
std::string NormalizeDtlsFingerprintValue(const std::string &fingerprint);

class DtlsTransport {
public:
    DtlsTransport();
    ~DtlsTransport();

    DtlsTransport(const DtlsTransport &) = delete;
    DtlsTransport &operator=(const DtlsTransport &) = delete;

    static bool IsDtlsPacket(const uint8_t *data, size_t size);
    static bool LocalCertificateFingerprint(DtlsFingerprint *fingerprint);

    bool StartServer(const DtlsFingerprint &remote_fingerprint);
    bool ProcessPacket(const uint8_t *data, size_t size,
                       DtlsProcessResult *result);
    bool GetHandshakeTimeoutMs(uint32_t *timeout_ms);
    bool HandleTimeout(DtlsProcessResult *result);
    void Close();

    DtlsState state() const { return state_; }

private:
    bool CreateSsl();
    bool FinishHandshake(DtlsProcessResult *result);
    bool CheckRemoteFingerprint();
    bool ExportSrtpKeys(DtlsProcessResult *result);
    bool SendPendingOutgoing(DtlsProcessResult *result);
    bool HandleSslResult(int ssl_result, DtlsProcessResult *result);
    bool Fail(const std::string &error, DtlsProcessResult *result);
    void ReleaseSsl();

    DtlsState state_ = DtlsState::kNew;
    DtlsFingerprint remote_fingerprint_;
    void *ssl_ = nullptr;
    void *read_bio_ = nullptr;
    void *write_bio_ = nullptr;
};

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_DTLS_TRANSPORT_H_
