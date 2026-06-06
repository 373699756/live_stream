#include "onvif_types.h"

#include <algorithm>
#include <cctype>

namespace live_stream {
namespace onvif {

bool Contains(const std::string &text, const std::string &needle) {
    return text.find(needle) != std::string::npos;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return value;
}

std::string XmlEscape(const std::string &value) {
    std::string escaped;
    for (char c : value) {
        switch (c) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            default:
                escaped.push_back(c);
                break;
        }
    }
    return escaped;
}

std::string StreamToken(StreamId stream_id) {
    return stream_id == StreamId::kSub ? "profile_sub" : "profile_main";
}

const char *ActionName(OnvifAction action) {
    switch (action) {
        case OnvifAction::kGetDeviceInformation:
            return "GetDeviceInformation";
        case OnvifAction::kGetSystemDateAndTime:
            return "GetSystemDateAndTime";
        case OnvifAction::kSetSystemDateAndTime:
            return "SetSystemDateAndTime";
        case OnvifAction::kGetProfiles:
            return "GetProfiles";
        case OnvifAction::kGetStreamUri:
            return "GetStreamUri";
        case OnvifAction::kGetSnapshotUri:
            return "GetSnapshotUri";
        case OnvifAction::kUnknown:
            return "Unknown";
    }
    return "Unknown";
}

}  // namespace onvif
}  // namespace live_stream
