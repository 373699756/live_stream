#ifndef LIVE_STREAM_ONVIF_SRC_ONVIF_DISCOVERY_H_
#define LIVE_STREAM_ONVIF_SRC_ONVIF_DISCOVERY_H_

#include "onvif_server.h"

#include <string>

namespace live_stream {
namespace onvif {

bool IsOnvifProbeRequest(const std::string &request);
std::string BuildDiscoveryProbeMatches(
    const OnvifServerOptions &options,
    ISystem *system,
    const std::string &advertise_ip,
    const std::string &request);

}  // namespace onvif
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SRC_ONVIF_DISCOVERY_H_
