#include "auth_internal.h"

#include <cstdio>

namespace live_stream {
namespace auth_internal {

bool IsEmptyOrTooLong(const std::string& value, std::size_t max_length) {
    return value.empty() || value.size() > max_length;
}

bool ParseRole(const std::string& role, AuthRole* parsed) {
    if (parsed == nullptr) {
        return false;
    }
    if (role == "admin") {
        *parsed = AuthRole::kAdmin;
        return true;
    }
    if (role == "operator") {
        *parsed = AuthRole::kOperator;
        return true;
    }
    if (role == "viewer") {
        *parsed = AuthRole::kViewer;
        return true;
    }
    return false;
}

bool IsPermissionAllowed(AuthRole role, AuthPermission permission) {
    switch (role) {
        case AuthRole::kAdmin:
            return true;
        case AuthRole::kOperator:
            return permission == AuthPermission::kReadStatus ||
                   permission == AuthPermission::kPreviewVideo ||
                   permission == AuthPermission::kModifyConfig;
        case AuthRole::kViewer:
            return permission == AuthPermission::kReadStatus ||
                   permission == AuthPermission::kPreviewVideo;
    }
    return false;
}

std::string MakeHexToken(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    char buffer[80] = {0};
    std::snprintf(buffer, sizeof(buffer),
                  "%016llx%016llx%016llx%016llx",
                  static_cast<unsigned long long>(a),
                  static_cast<unsigned long long>(b),
                  static_cast<unsigned long long>(c),
                  static_cast<unsigned long long>(d));
    return std::string(buffer);
}

}  // namespace auth_internal
}  // namespace live_stream
