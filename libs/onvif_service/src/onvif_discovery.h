#ifndef LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_DISCOVERY_H_
#define LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_DISCOVERY_H_

#include "onvif_service.h"

#include <string>

namespace live_stream {
namespace onvif_internal {

std::string DiscoveryProbeMatchesBody(const OnvifServiceOptions& options,
                                      const std::string& advertise_ip);

}  // namespace onvif_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_DISCOVERY_H_
