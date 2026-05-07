#ifndef LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_SOAP_H_
#define LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_SOAP_H_

#include "onvif_types.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace onvif_internal {

std::string SoapEnvelope(const std::string& body);
std::string SoapFault(const std::string& reason);
OnvifAction ParseAction(const std::string& body);
bool ExtractInt64Tag(const std::string& text,
                     const std::string& tag,
                     int64_t* value);
bool ParseOnvifUnixTimeMs(const std::string& request, int64_t* unix_time_ms);

}  // namespace onvif_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_SOAP_H_
