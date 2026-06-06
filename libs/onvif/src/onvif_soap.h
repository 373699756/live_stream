#ifndef LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_SOAP_H_
#define LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_SOAP_H_

#include "onvif_types.h"

#include <cstdint>
#include <string>

namespace live_stream {
namespace onvif {

std::string BuildSoapEnvelope(const std::string &body);
std::string BuildSoapFaultBody(const std::string &reason);
std::string BuildSoapFaultEnvelope(const std::string &reason);
OnvifAction ParseSoapAction(const std::string &body);
bool ExtractXmlTagText(const std::string &text,
                       const std::string &local_name,
                       std::string *value);
bool ExtractInt64Tag(const std::string &text,
                     const std::string &local_name,
                     int64_t *value);
bool ParseOnvifUnixTimeMs(const std::string &request, int64_t *unix_time_ms);

}  // namespace onvif
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_SOAP_H_
