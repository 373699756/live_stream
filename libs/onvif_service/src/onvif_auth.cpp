#include "onvif_auth.h"

#include <cctype>

namespace live_stream {
namespace onvif {
namespace {

constexpr const char *kOnvifServerName = "onvif_service";

std::string Base64Decode(const std::string &encoded) {
    static const std::string kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded;
    int value = 0;
    int bits = -8;
    for (char c : encoded) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            continue;
        }
        if (c == '=') {
            break;
        }
        const std::size_t index = kAlphabet.find(c);
        if (index == std::string::npos) {
            return "";
        }
        value = (value << 6) + static_cast<int>(index);
        bits += 6;
        if (bits >= 0) {
            decoded.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return decoded;
}

bool ExtractBasicCredentials(const std::string &headers,
                             std::string *user_name,
                             std::string *password) {
    const std::string lower = ToLower(headers);
    const std::string marker = "authorization: basic ";
    const std::size_t begin = lower.find(marker);
    if (begin == std::string::npos || user_name == nullptr ||
        password == nullptr) {
        return false;
    }
    const std::size_t value_begin = begin + marker.size();
    std::size_t value_end = headers.find("\r\n", value_begin);
    if (value_end == std::string::npos) {
        value_end = headers.find('\n', value_begin);
    }
    if (value_end == std::string::npos) {
        value_end = headers.size();
    }
    const std::string decoded =
        Base64Decode(headers.substr(value_begin, value_end - value_begin));
    const std::size_t separator = decoded.find(':');
    if (separator == std::string::npos) {
        return false;
    }
    *user_name = decoded.substr(0, separator);
    *password = decoded.substr(separator + 1);
    return true;
}

AuthPermission PermissionForOnvifAction(OnvifAction action) {
    switch (action) {
        case OnvifAction::kGetStreamUri:
        case OnvifAction::kGetSnapshotUri:
            return AuthPermission::kPreviewVideo;
        case OnvifAction::kSetSystemDateAndTime:
            return AuthPermission::kModifyConfig;
        case OnvifAction::kGetDeviceInformation:
        case OnvifAction::kGetSystemDateAndTime:
        case OnvifAction::kGetProfiles:
        case OnvifAction::kUnknown:
            return AuthPermission::kReadStatus;
    }
    return AuthPermission::kReadStatus;
}

}  // namespace

bool AuthorizeOnvifAction(IAuthService *auth_service,
                          bool enable_auth,
                          const std::string &headers,
                          OnvifAction action) {
    if (!enable_auth) {
        return true;
    }
    if (auth_service == nullptr) {
        return false;
    }
    std::string user_name;
    std::string password;
    if (!ExtractBasicCredentials(headers, &user_name, &password)) {
        return false;
    }
    LoginRequest login;
    login.context.client_ip = "onvif";
    login.user_name = user_name;
    login.password = password;
    LoginResult result = auth_service->Login(login);
    if (result.token.empty()) {
        return false;
    }
    live_stream::RequestContext logout_context;
    logout_context.user_name = result.principal.user_name;
    logout_context.session_id = result.principal.session_id;
    if (result.must_change_password) {
        static_cast<void>(auth_service->Logout(logout_context));
        return false;
    }
    const bool allowed = auth_service->CheckPermission(
        result.principal, PermissionForOnvifAction(action), kOnvifServerName);
    static_cast<void>(auth_service->Logout(logout_context));
    return allowed;
}

}  // namespace onvif
}  // namespace live_stream
