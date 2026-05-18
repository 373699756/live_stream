#include "onvif_discovery.h"

#include "onvif_soap.h"

namespace live_stream {
namespace onvif_internal {

std::string DiscoveryProbeMatchesBody(const OnvifServiceOptions& options,
                                      const std::string& advertise_ip) {
    return SoapEnvelope(
        "<d:ProbeMatches xmlns:d=\"http://schemas.xmlsoap.org/"
        "ws/2005/04/discovery\"><d:ProbeMatch><d:Types>"
        "dn:NetworkVideoTransmitter</d:Types><d:XAddrs>http://" +
        advertise_ip + ":" +
        std::to_string(options.device_service_port) +
        options.service_path +
        "</d:XAddrs></d:ProbeMatch></d:ProbeMatches>");
}

}  // namespace onvif_internal
}  // namespace live_stream
