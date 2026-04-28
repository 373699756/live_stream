#include "auth_internal.h"

#include <memory>
#include <string>

namespace live_stream {
namespace {

class PlainTextPasswordVerifier : public IPasswordVerifier {
 public:
    infra::Status VerifyPassword(
        const std::string& password,
        const std::string& password_credential) override {
        if (password == password_credential) {
            return infra::Status::kOk;
        }
        return infra::Status::kUnauthorized;
    }
};

class Sha256PasswordVerifier : public IPasswordVerifier {
 public:
    infra::Status VerifyPassword(
        const std::string& password,
        const std::string& password_credential) override {
        const std::string prefix = "sha256:";
        if (password_credential.compare(0, prefix.size(), prefix) != 0) {
            return infra::Status::kInvalidParam;
        }
        const std::size_t salt_begin = prefix.size();
        const std::size_t separator = password_credential.find(':', salt_begin);
        if (separator == std::string::npos) {
            return infra::Status::kInvalidParam;
        }
        const std::string salt_hex =
            password_credential.substr(salt_begin, separator - salt_begin);
        infra::Result<std::string> expected =
            auth_internal::Sha256Credential(password, salt_hex);
        if (!expected.IsOk()) {
            return expected.status;
        }
        if (auth_internal::ConstantTimeEquals(expected.value,
                                              password_credential)) {
            return infra::Status::kOk;
        }
        return infra::Status::kUnauthorized;
    }
};

}  // namespace

std::unique_ptr<IPasswordVerifier> CreatePlainTextPasswordVerifier() {
    return std::unique_ptr<IPasswordVerifier>(new PlainTextPasswordVerifier());
}

std::unique_ptr<IPasswordVerifier> CreateSha256PasswordVerifier() {
    return std::unique_ptr<IPasswordVerifier>(new Sha256PasswordVerifier());
}

infra::Result<std::string> MakeSha256PasswordCredential(
    const std::string& password,
    const std::string& salt_hex) {
    return auth_internal::Sha256Credential(password, salt_hex);
}

}  // namespace live_stream
