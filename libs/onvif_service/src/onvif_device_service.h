#ifndef LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_DEVICE_SERVICE_H_
#define LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_DEVICE_SERVICE_H_

#include "onvif_server.h"

#include <string>

namespace live_stream {
namespace onvif {

std::string BuildDeviceInformationBody(const OnvifServerOptions &options,
                                       ISystemService *system_service);
std::string BuildSystemDateAndTimeBody(ITimeService *time_service);
std::string BuildSetSystemDateAndTimeBody(ITimeService *time_service,
                                          const std::string &request,
                                          uint32_t *status,
                                          std::string *reason);

}  // namespace onvif
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_DEVICE_SERVICE_H_
