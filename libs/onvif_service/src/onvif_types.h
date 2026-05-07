#ifndef LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_TYPES_H_
#define LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_TYPES_H_

#include "media/stream_types.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace onvif_internal {

constexpr uint32_t kMaxHttpResponseBytes = 32 * 1024;

enum class OnvifAction {
    kUnknown,
    kGetDeviceInformation,
    kGetSystemDateAndTime,
    kSetSystemDateAndTime,
    kGetProfiles,
    kGetStreamUri,
    kGetSnapshotUri,
};

bool Contains(const std::string& text, const std::string& needle);
std::string ToLower(std::string value);
std::string XmlEscape(const std::string& value);
std::string StreamToken(StreamId stream_id);
const char* ActionName(OnvifAction action);

}  // namespace onvif_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_TYPES_H_
