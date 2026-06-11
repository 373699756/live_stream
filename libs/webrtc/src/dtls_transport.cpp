#include "dtls_transport.h"

#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>

namespace live_stream {
namespace webrtc_internal {
namespace {

constexpr size_t kDtlsMtu = 1200;
constexpr size_t kSslReadBufferSize = 2000;
constexpr size_t kAes128MasterKeySize = 16;
constexpr size_t kAes128MasterSaltSize = 14;
constexpr size_t kAes128MasterKeyAndSaltSize =
    kAes128MasterKeySize + kAes128MasterSaltSize;
constexpr const char *kSrtpProfile = "SRTP_AES128_CM_SHA1_80";
constexpr const char *kSrtpKeyExporterLabel = "EXTRACTOR-dtls_srtp";

struct DtlsEnvironment {
    SSL_CTX *ssl_ctx = nullptr;
    X509 *certificate = nullptr;
    EVP_PKEY *private_key = nullptr;
    DtlsFingerprint sha256_fingerprint;
    bool ready = false;

    ~DtlsEnvironment() {
        if (ssl_ctx != nullptr) {
            SSL_CTX_free(ssl_ctx);
        }
        if (certificate != nullptr) {
            X509_free(certificate);
        }
        if (private_key != nullptr) {
            EVP_PKEY_free(private_key);
        }
    }
};

std::mutex &EnvironmentMutex() {
    static std::mutex mutex;
    return mutex;
}

DtlsEnvironment &Environment() {
    static DtlsEnvironment environment;
    return environment;
}

bool IsHexDigit(char ch) {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

std::string ToUpperAscii(const std::string &text) {
    std::string result;
    result.reserve(text.size());
    for (char ch : text) {
        result.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch))));
    }
    return result;
}

std::string FingerprintToHex(const uint8_t *data, size_t size) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(size * 3);
    for (size_t i = 0; i < size; ++i) {
        if (i != 0) {
            result.push_back(':');
        }
        result.push_back(kHex[(data[i] >> 4) & 0x0f]);
        result.push_back(kHex[data[i] & 0x0f]);
    }
    return result;
}

const EVP_MD *DigestForAlgorithm(DtlsFingerprintAlgorithm algorithm) {
    switch (algorithm) {
        case DtlsFingerprintAlgorithm::kSha1:
            return EVP_sha1();
        case DtlsFingerprintAlgorithm::kSha224:
            return EVP_sha224();
        case DtlsFingerprintAlgorithm::kSha256:
            return EVP_sha256();
        case DtlsFingerprintAlgorithm::kSha384:
            return EVP_sha384();
        case DtlsFingerprintAlgorithm::kSha512:
            return EVP_sha512();
        case DtlsFingerprintAlgorithm::kNone:
            return nullptr;
    }
    return nullptr;
}

bool BuildFingerprint(X509 *certificate, DtlsFingerprintAlgorithm algorithm,
                      DtlsFingerprint *fingerprint) {
    if (certificate == nullptr || fingerprint == nullptr) {
        return false;
    }
    const EVP_MD *digest = DigestForAlgorithm(algorithm);
    if (digest == nullptr) {
        return false;
    }
    uint8_t binary_fingerprint[EVP_MAX_MD_SIZE];
    unsigned int fingerprint_size = 0;
    if (X509_digest(certificate, digest, binary_fingerprint,
                    &fingerprint_size) != 1 ||
        fingerprint_size == 0) {
        return false;
    }
    fingerprint->algorithm = algorithm;
    fingerprint->value = FingerprintToHex(binary_fingerprint, fingerprint_size);
    return true;
}

bool GenerateCertificate(DtlsEnvironment *environment) {
    if (environment == nullptr) {
        return false;
    }

    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (ec_key == nullptr) {
        return false;
    }
    EC_KEY_set_asn1_flag(ec_key, OPENSSL_EC_NAMED_CURVE);
    if (EC_KEY_generate_key(ec_key) != 1) {
        EC_KEY_free(ec_key);
        return false;
    }

    EVP_PKEY *private_key = EVP_PKEY_new();
    if (private_key == nullptr) {
        EC_KEY_free(ec_key);
        return false;
    }
    if (EVP_PKEY_assign_EC_KEY(private_key, ec_key) != 1) {
        EVP_PKEY_free(private_key);
        EC_KEY_free(ec_key);
        return false;
    }
    ec_key = nullptr;

    X509 *certificate = X509_new();
    if (certificate == nullptr) {
        EVP_PKEY_free(private_key);
        return false;
    }
    if (X509_set_version(certificate, 2) != 1) {
        X509_free(certificate);
        EVP_PKEY_free(private_key);
        return false;
    }
    ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1);
    X509_gmtime_adj(X509_get_notBefore(certificate), -31536000);
    X509_gmtime_adj(X509_get_notAfter(certificate), 315360000);
    if (X509_set_pubkey(certificate, private_key) != 1) {
        X509_free(certificate);
        EVP_PKEY_free(private_key);
        return false;
    }

    X509_NAME *name = X509_get_subject_name(certificate);
    const char kSubject[] = "live-stream-webrtc";
    if (name == nullptr ||
        X509_NAME_add_entry_by_txt(
            name, "O", MBSTRING_ASC,
            reinterpret_cast<const unsigned char *>(kSubject), -1, -1, 0) != 1 ||
        X509_NAME_add_entry_by_txt(
            name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char *>(kSubject), -1, -1, 0) != 1 ||
        X509_set_issuer_name(certificate, name) != 1 ||
        X509_sign(certificate, private_key, EVP_sha256()) == 0) {
        X509_free(certificate);
        EVP_PKEY_free(private_key);
        return false;
    }

    environment->certificate = certificate;
    environment->private_key = private_key;
    return true;
}

int AcceptAnyCertificate(int preverify_ok, X509_STORE_CTX *store_ctx) {
    (void)preverify_ok;
    (void)store_ctx;
    return 1;
}

bool BuildSslContext(DtlsEnvironment *environment) {
    if (environment == nullptr || environment->certificate == nullptr ||
        environment->private_key == nullptr) {
        return false;
    }

    SSL_CTX *ssl_ctx = SSL_CTX_new(DTLS_method());
    if (ssl_ctx == nullptr) {
        return false;
    }
    if (SSL_CTX_use_certificate(ssl_ctx, environment->certificate) != 1 ||
        SSL_CTX_use_PrivateKey(ssl_ctx, environment->private_key) != 1 ||
        SSL_CTX_check_private_key(ssl_ctx) != 1) {
        SSL_CTX_free(ssl_ctx);
        return false;
    }

    SSL_CTX_set_options(ssl_ctx, SSL_OP_CIPHER_SERVER_PREFERENCE |
                                     SSL_OP_NO_TICKET |
                                     SSL_OP_SINGLE_ECDH_USE |
                                     SSL_OP_NO_QUERY_MTU);
    SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_OFF);
    SSL_CTX_set_read_ahead(ssl_ctx, 1);
    SSL_CTX_set_verify_depth(ssl_ctx, 4);
    SSL_CTX_set_verify(ssl_ctx,
                       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       AcceptAnyCertificate);
    if (SSL_CTX_set_cipher_list(
            ssl_ctx,
            "DEFAULT:!NULL:!aNULL:!SHA256:!SHA384:!aECDH:!AESGCM+AES256:"
            "!aPSK:!RC4") != 1) {
        SSL_CTX_free(ssl_ctx);
        return false;
    }
    if (SSL_CTX_set1_curves_list(ssl_ctx, "P-256:X25519") != 1) {
        SSL_CTX_free(ssl_ctx);
        return false;
    }
    if (SSL_CTX_set_tlsext_use_srtp(ssl_ctx, kSrtpProfile) != 0) {
        SSL_CTX_free(ssl_ctx);
        return false;
    }

    environment->ssl_ctx = ssl_ctx;
    return true;
}

bool PrepareEnvironmentLocked() {
    DtlsEnvironment &environment = Environment();
    if (environment.ready) {
        return true;
    }
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    if (!GenerateCertificate(&environment) || !BuildSslContext(&environment) ||
        !BuildFingerprint(environment.certificate,
                          DtlsFingerprintAlgorithm::kSha256,
                          &environment.sha256_fingerprint)) {
        if (environment.ssl_ctx != nullptr) {
            SSL_CTX_free(environment.ssl_ctx);
            environment.ssl_ctx = nullptr;
        }
        if (environment.certificate != nullptr) {
            X509_free(environment.certificate);
            environment.certificate = nullptr;
        }
        if (environment.private_key != nullptr) {
            EVP_PKEY_free(environment.private_key);
            environment.private_key = nullptr;
        }
        environment.sha256_fingerprint = DtlsFingerprint();
        return false;
    }
    environment.ready = true;
    return true;
}

bool EnsureEnvironment() {
    std::lock_guard<std::mutex> guard(EnvironmentMutex());
    return PrepareEnvironmentLocked();
}

SSL *AsSsl(void *ssl) { return static_cast<SSL *>(ssl); }
BIO *AsBio(void *bio) { return static_cast<BIO *>(bio); }

bool IsHandshakeWantIo(SSL *ssl, int ssl_result) {
    const int ssl_error = SSL_get_error(ssl, ssl_result);
    return ssl_error == SSL_ERROR_WANT_READ ||
           ssl_error == SSL_ERROR_WANT_WRITE;
}

}  // namespace

const char *DtlsFingerprintAlgorithmName(DtlsFingerprintAlgorithm algorithm) {
    switch (algorithm) {
        case DtlsFingerprintAlgorithm::kSha1:
            return "sha-1";
        case DtlsFingerprintAlgorithm::kSha224:
            return "sha-224";
        case DtlsFingerprintAlgorithm::kSha256:
            return "sha-256";
        case DtlsFingerprintAlgorithm::kSha384:
            return "sha-384";
        case DtlsFingerprintAlgorithm::kSha512:
            return "sha-512";
        case DtlsFingerprintAlgorithm::kNone:
            return "";
    }
    return "";
}

DtlsFingerprintAlgorithm DtlsFingerprintAlgorithmFromString(
    const std::string &algorithm) {
    const std::string normalized = ToUpperAscii(algorithm);
    if (normalized == "SHA-1" || normalized == "SHA1") {
        return DtlsFingerprintAlgorithm::kSha1;
    }
    if (normalized == "SHA-224" || normalized == "SHA224") {
        return DtlsFingerprintAlgorithm::kSha224;
    }
    if (normalized == "SHA-256" || normalized == "SHA256") {
        return DtlsFingerprintAlgorithm::kSha256;
    }
    if (normalized == "SHA-384" || normalized == "SHA384") {
        return DtlsFingerprintAlgorithm::kSha384;
    }
    if (normalized == "SHA-512" || normalized == "SHA512") {
        return DtlsFingerprintAlgorithm::kSha512;
    }
    return DtlsFingerprintAlgorithm::kNone;
}

std::string NormalizeDtlsFingerprintValue(const std::string &fingerprint) {
    std::string hex;
    hex.reserve(fingerprint.size());
    for (char ch : fingerprint) {
        if (IsHexDigit(ch)) {
            hex.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch))));
        }
    }
    if (hex.size() % 2 != 0) {
        return std::string();
    }

    std::string normalized;
    normalized.reserve(hex.size() + hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        if (i != 0) {
            normalized.push_back(':');
        }
        normalized.push_back(hex[i]);
        normalized.push_back(hex[i + 1]);
    }
    return normalized;
}

DtlsTransport::DtlsTransport() = default;

DtlsTransport::~DtlsTransport() { Close(); }

bool DtlsTransport::IsDtlsPacket(const uint8_t *data, size_t size) {
    return data != nullptr && size >= 13 && data[0] > 19 && data[0] < 64;
}

bool DtlsTransport::LocalCertificateFingerprint(DtlsFingerprint *fingerprint) {
    if (fingerprint == nullptr || !EnsureEnvironment()) {
        return false;
    }
    std::lock_guard<std::mutex> guard(EnvironmentMutex());
    *fingerprint = Environment().sha256_fingerprint;
    return !fingerprint->value.empty();
}

bool DtlsTransport::StartServer(const DtlsFingerprint &remote_fingerprint) {
    if (remote_fingerprint.algorithm == DtlsFingerprintAlgorithm::kNone ||
        remote_fingerprint.value.empty()) {
        state_ = DtlsState::kFailed;
        return false;
    }
    Close();
    remote_fingerprint_ = remote_fingerprint;
    if (!CreateSsl()) {
        state_ = DtlsState::kFailed;
        return false;
    }
    SSL_set_accept_state(AsSsl(ssl_));
    const int handshake_result = SSL_do_handshake(AsSsl(ssl_));
    DtlsProcessResult ignored;
    if (!SendPendingOutgoing(&ignored) ||
        (!IsHandshakeWantIo(AsSsl(ssl_), handshake_result) &&
         handshake_result != 1)) {
        Fail("dtls_handshake_start_failed", &ignored);
        return false;
    }
    if (handshake_result == 1) {
        return FinishHandshake(&ignored);
    }
    state_ = DtlsState::kConnecting;
    return true;
}

bool DtlsTransport::ProcessPacket(const uint8_t *data, size_t size,
                                  DtlsProcessResult *result) {
    if (result == nullptr) {
        return false;
    }
    *result = DtlsProcessResult();
    result->state = state_;
    if (state_ != DtlsState::kConnecting && state_ != DtlsState::kConnected) {
        result->error = "dtls_not_running";
        return false;
    }
    if (!IsDtlsPacket(data, size)) {
        result->error = "not_dtls_packet";
        return false;
    }

    const int written =
        BIO_write(AsBio(read_bio_), data, static_cast<int>(size));
    if (written != static_cast<int>(size)) {
        return Fail("dtls_bio_write_failed", result);
    }

    int read_result = SSL_do_handshake(AsSsl(ssl_));
    if (!SendPendingOutgoing(result)) {
        return Fail("dtls_send_pending_failed", result);
    }
    if (!HandleSslResult(read_result, result)) {
        return false;
    }
    if (state_ == DtlsState::kConnected) {
        uint8_t buffer[kSslReadBufferSize];
        read_result =
            SSL_read(AsSsl(ssl_), buffer, static_cast<int>(sizeof(buffer)));
        if (!SendPendingOutgoing(result)) {
            return Fail("dtls_send_pending_failed", result);
        }
        if (read_result > 0) {
            result->application_data.assign(buffer, buffer + read_result);
        } else if (!HandleSslResult(read_result, result)) {
            return false;
        }
    }
    result->state = state_;
    return state_ == DtlsState::kConnecting || state_ == DtlsState::kConnected;
}

bool DtlsTransport::GetHandshakeTimeoutMs(uint32_t *timeout_ms) {
    if (timeout_ms == nullptr || ssl_ == nullptr ||
        state_ != DtlsState::kConnecting) {
        return false;
    }
    timeval timeout;
    if (DTLSv1_get_timeout(AsSsl(ssl_), &timeout) == 0) {
        return false;
    }
    const uint64_t timeout_value =
        static_cast<uint64_t>(timeout.tv_sec) * 1000 +
        static_cast<uint64_t>(timeout.tv_usec) / 1000;
    if (timeout_value == 0 || timeout_value > 30000) {
        return false;
    }
    *timeout_ms = static_cast<uint32_t>(timeout_value);
    return true;
}

bool DtlsTransport::HandleTimeout(DtlsProcessResult *result) {
    if (result == nullptr) {
        return false;
    }
    *result = DtlsProcessResult();
    result->state = state_;
    if (ssl_ == nullptr || state_ != DtlsState::kConnecting) {
        result->error = "dtls_not_running";
        return false;
    }
    if (DTLSv1_handle_timeout(AsSsl(ssl_)) < 0) {
        return Fail("dtls_timeout_failed", result);
    }
    if (!SendPendingOutgoing(result)) {
        return Fail("dtls_send_pending_failed", result);
    }
    if (SSL_is_init_finished(AsSsl(ssl_)) == 1 &&
        state_ != DtlsState::kConnected) {
        return FinishHandshake(result);
    }
    result->state = state_;
    return true;
}

void DtlsTransport::Close() {
    if (ssl_ != nullptr &&
        (state_ == DtlsState::kConnecting || state_ == DtlsState::kConnected)) {
        (void)SSL_shutdown(AsSsl(ssl_));
    }
    ReleaseSsl();
    remote_fingerprint_ = DtlsFingerprint();
    if (state_ != DtlsState::kFailed) {
        state_ = DtlsState::kClosed;
    }
}

bool DtlsTransport::CreateSsl() {
    if (!EnsureEnvironment()) {
        return false;
    }
    std::lock_guard<std::mutex> guard(EnvironmentMutex());
    SSL *ssl = SSL_new(Environment().ssl_ctx);
    if (ssl == nullptr) {
        return false;
    }
    BIO *read_bio = BIO_new(BIO_s_mem());
    BIO *write_bio = BIO_new(BIO_s_mem());
    if (read_bio == nullptr || write_bio == nullptr) {
        if (read_bio != nullptr) {
            BIO_free(read_bio);
        }
        if (write_bio != nullptr) {
            BIO_free(write_bio);
        }
        SSL_free(ssl);
        return false;
    }
    SSL_set_bio(ssl, read_bio, write_bio);
    SSL_set_mtu(ssl, static_cast<long>(kDtlsMtu));
    DTLS_set_link_mtu(ssl, static_cast<long>(kDtlsMtu));
    ssl_ = ssl;
    read_bio_ = read_bio;
    write_bio_ = write_bio;
    return true;
}

bool DtlsTransport::FinishHandshake(DtlsProcessResult *result) {
    if (!CheckRemoteFingerprint()) {
        return Fail("dtls_fingerprint_mismatch", result);
    }
    if (!ExportSrtpKeys(result)) {
        return Fail("dtls_srtp_key_export_failed", result);
    }
    state_ = DtlsState::kConnected;
    result->state = state_;
    return true;
}

bool DtlsTransport::CheckRemoteFingerprint() {
    if (remote_fingerprint_.algorithm == DtlsFingerprintAlgorithm::kNone ||
        remote_fingerprint_.value.empty() || ssl_ == nullptr) {
        return false;
    }
    X509 *certificate = SSL_get_peer_certificate(AsSsl(ssl_));
    if (certificate == nullptr) {
        return false;
    }
    DtlsFingerprint actual;
    const bool built = BuildFingerprint(certificate, remote_fingerprint_.algorithm,
                                        &actual);
    X509_free(certificate);
    return built && actual.value == remote_fingerprint_.value;
}

bool DtlsTransport::ExportSrtpKeys(DtlsProcessResult *result) {
    if (result == nullptr || ssl_ == nullptr) {
        return false;
    }
    const SRTP_PROTECTION_PROFILE *profile =
        SSL_get_selected_srtp_profile(AsSsl(ssl_));
    if (profile == nullptr || std::strcmp(profile->name, kSrtpProfile) != 0) {
        return false;
    }

    uint8_t key_material[kAes128MasterKeyAndSaltSize * 2];
    if (SSL_export_keying_material(
            AsSsl(ssl_), key_material, sizeof(key_material),
            kSrtpKeyExporterLabel, std::strlen(kSrtpKeyExporterLabel), nullptr, 0,
            0) != 1) {
        return false;
    }

    const uint8_t *remote_key = key_material;
    const uint8_t *local_key = remote_key + kAes128MasterKeySize;
    const uint8_t *remote_salt = local_key + kAes128MasterKeySize;
    const uint8_t *local_salt = remote_salt + kAes128MasterSaltSize;

    result->srtp_keys.suite = DtlsSrtpCryptoSuite::kAes128CmSha1_80;
    result->srtp_keys.local_master_key.assign(
        local_key, local_key + kAes128MasterKeySize);
    result->srtp_keys.local_master_key.insert(
        result->srtp_keys.local_master_key.end(), local_salt,
        local_salt + kAes128MasterSaltSize);
    result->srtp_keys.remote_master_key.assign(
        remote_key, remote_key + kAes128MasterKeySize);
    result->srtp_keys.remote_master_key.insert(
        result->srtp_keys.remote_master_key.end(), remote_salt,
        remote_salt + kAes128MasterSaltSize);
    return true;
}

bool DtlsTransport::SendPendingOutgoing(DtlsProcessResult *result) {
    if (result == nullptr || write_bio_ == nullptr) {
        return false;
    }
    uint8_t buffer[kSslReadBufferSize];
    while (BIO_ctrl_pending(AsBio(write_bio_)) > 0) {
        const int read =
            BIO_read(AsBio(write_bio_), buffer, static_cast<int>(sizeof(buffer)));
        if (read <= 0) {
            return false;
        }
        result->outgoing_dtls.insert(result->outgoing_dtls.end(), buffer,
                                     buffer + read);
    }
    return true;
}

bool DtlsTransport::HandleSslResult(int ssl_result,
                                    DtlsProcessResult *result) {
    if (result == nullptr || ssl_ == nullptr) {
        return false;
    }
    if (ssl_result > 0) {
        if (SSL_is_init_finished(AsSsl(ssl_)) == 1 &&
            state_ != DtlsState::kConnected) {
            return FinishHandshake(result);
        }
        result->state = state_;
        return true;
    }

    const int ssl_error = SSL_get_error(AsSsl(ssl_), ssl_result);
    if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
        if (SSL_is_init_finished(AsSsl(ssl_)) == 1 &&
            state_ != DtlsState::kConnected) {
            return FinishHandshake(result);
        }
        result->state = state_;
        return true;
    }
    if (ssl_error == SSL_ERROR_ZERO_RETURN) {
        state_ = DtlsState::kClosed;
        result->state = state_;
        result->error = "dtls_closed";
        return false;
    }
    return Fail("dtls_handshake_failed", result);
}

bool DtlsTransport::Fail(const std::string &error, DtlsProcessResult *result) {
    ReleaseSsl();
    state_ = DtlsState::kFailed;
    if (result != nullptr) {
        result->state = state_;
        result->error = error;
    }
    return false;
}

void DtlsTransport::ReleaseSsl() {
    if (ssl_ != nullptr) {
        SSL_free(AsSsl(ssl_));
    }
    ssl_ = nullptr;
    read_bio_ = nullptr;
    write_bio_ = nullptr;
}

}  // namespace webrtc_internal
}  // namespace live_stream
