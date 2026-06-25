#include "auth_internal.h"

#include <cstdlib>
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

class Pbkdf2PasswordVerifier : public IPasswordVerifier {
public:
    bool VerifyPassword(
        const std::string& password,
        const std::string& password_credential) override {
        const std::string prefix = "pbkdf2-sha256:";
        if (password_credential.compare(0, prefix.size(), prefix) != 0) {
            return false;
        }
        const std::size_t iterations_begin = prefix.size();
        const std::size_t iterations_end =
            password_credential.find(':', iterations_begin);
        if (iterations_end == std::string::npos) {
            return false;
        }
        const std::string iterations_text = password_credential.substr(
            iterations_begin, iterations_end - iterations_begin);
        char* end = nullptr;
        const unsigned long iterations =
            std::strtoul(iterations_text.c_str(), &end, 10);
        if (end == iterations_text.c_str() || *end != '\0' ||
            iterations != auth_internal::kPasswordPbkdf2Iterations) {
            return false;
        }

        const std::size_t salt_begin = iterations_end + 1;
        const std::size_t salt_end = password_credential.find(':', salt_begin);
        if (salt_end == std::string::npos) {
            return false;
        }
        const std::string salt_hex =
            password_credential.substr(salt_begin, salt_end - salt_begin);
        const std::string expected = auth_internal::Pbkdf2Sha256Credential(
            password, salt_hex, static_cast<uint32_t>(iterations));
        return !expected.empty() &&
               auth_internal::ConstantTimeEquals(expected,
                                                 password_credential);
    }
};

}  // namespace

std::unique_ptr<IPasswordVerifier> CreatePasswordVerifier(
    PasswordVerifierKind kind) {
    if (kind == PasswordVerifierKind::kPlainText) {
        return std::unique_ptr<IPasswordVerifier>(
            new PlainTextPasswordVerifier());
    }
    if (kind == PasswordVerifierKind::kPbkdf2) {
        return std::unique_ptr<IPasswordVerifier>(
            new Pbkdf2PasswordVerifier());
    }
    return nullptr;
}

}  // namespace live_stream
