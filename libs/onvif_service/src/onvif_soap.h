#ifndef LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_SOAP_H_
#define LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_SOAP_H_

#include "infra/status.h"
#include "onvif_types.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace onvif_internal {

std::string SoapEnvelope(const std::string& body);
std::string SoapFault(const std::string& reason);
OnvifAction ParseAction(const std::string& body);
infra::Result<int64_t> ExtractInt64Tag(const std::string& text,
                                       const std::string& tag);
infra::Result<int64_t> ParseOnvifUnixTimeMs(const std::string& request);

}  // namespace onvif_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_SOAP_H_
