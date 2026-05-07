#include "auth_internal.h"

#include <memory>
#include <string>

namespace live_stream {
namespace {

class PlainTextPasswordVerifier : public IPasswordVerifier {
 public:
    bool VerifyPassword(
        const std::string& password,
        const std::string& password_credential) override {
        return password == password_credential;
    }
};

class Sha256PasswordVerifier : public IPasswordVerifier {
 public:
    bool VerifyPassword(
        const std::string& password,
        const std::string& password_credential) override {
        const std::string prefix = "sha256:";
        if (password_credential.compare(0, prefix.size(), prefix) != 0) {
            return false;
        }
        const std::size_t salt_begin = prefix.size();
        const std::size_t separator = password_credential.find(':', salt_begin);
        if (separator == std::string::npos) {
            return false;
        }
        const std::string salt_hex =
            password_credential.substr(salt_begin, separator - salt_begin);
        std::string expected =
            auth_internal::Sha256Credential(password, salt_hex);
        return !expected.empty() &&
               auth_internal::ConstantTimeEquals(expected,
                                                 password_credential);
    }
};

}  // namespace

std::unique_ptr<IPasswordVerifier> CreatePlainTextPasswordVerifier() {
    return std::unique_ptr<IPasswordVerifier>(new PlainTextPasswordVerifier());
}

std::unique_ptr<IPasswordVerifier> CreateSha256PasswordVerifier() {
    return std::unique_ptr<IPasswordVerifier>(new Sha256PasswordVerifier());
}

std::string MakeSha256PasswordCredential(
    const std::string& password,
    const std::string& salt_hex) {
    return auth_internal::Sha256Credential(password, salt_hex);
}

}  // namespace live_stream
