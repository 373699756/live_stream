#ifndef LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_DEVICE_H_
#define LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_DEVICE_H_

#include "onvif_media.h"
#include "onvif_service.h"

#include <string>

namespace live_stream {
namespace onvif_internal {

std::string DeviceInformationBody(const OnvifServiceOptions& options,
                                  ISystemService* system_service);
std::string SystemDateAndTimeBody(ITimeService* time_service);
std::string SetSystemDateAndTimeBody(ITimeService* time_service,
                                     const std::string& request,
                                     uint32_t* status,
                                     std::string* reason);

}  // namespace onvif_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_DEVICE_H_
