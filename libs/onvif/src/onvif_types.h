#ifndef LIVE_STREAM_ONVIF_SRC_ONVIF_TYPES_H_
#define LIVE_STREAM_ONVIF_SRC_ONVIF_TYPES_H_

#include "media/stream_types.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace onvif {

constexpr uint32_t kMaxOnvifHttpMessageBytes = 32 * 1024;

enum class OnvifAction {
    kUnknown,
    kGetDeviceInformation,
    kGetSystemDateAndTime,
    kSetSystemDateAndTime,
    kGetProfiles,
    kGetStreamUri,
    kGetSnapshotUri,
};

struct OnvifBody {
    std::string body;
    bool success = false;
};

bool Contains(const std::string &text, const std::string &needle);
std::string ToLower(std::string value);
std::string XmlEscape(const std::string &value);
std::string StreamToken(StreamId stream_id);
const char *ActionName(OnvifAction action);

}  // namespace onvif
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SRC_ONVIF_TYPES_H_
