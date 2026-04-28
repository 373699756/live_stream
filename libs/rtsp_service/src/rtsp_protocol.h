#ifndef LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_PROTOCOL_H_
#define LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_PROTOCOL_H_

#include "rtsp_service.h"

#include <map>
#include <string>

namespace live_stream {
namespace rtsp_internal {

struct RtspRequest {
    std::string method;
    std::string uri;
    std::map<std::string, std::string> headers;
};

std::string Trim(const std::string& value);
std::string Lower(std::string value);
bool ContainsNoCase(const std::string& value, const std::string& needle);
std::string HeaderValue(const RtspRequest& request, const std::string& name);
bool ParseRtspRequest(const std::string& raw, RtspRequest* request);
std::string CSeq(const RtspRequest& request);
std::string BuildRtspResponse(int status,
                              const std::string& cseq,
                              const std::map<std::string, std::string>& headers,
                              const std::string& body);
bool PathToStreamId(const std::string& uri, infra::StreamId* stream_id);
const char* StreamPath(infra::StreamId stream_id);
std::string BuildSdp(const RtspListenAddress& address,
                     infra::StreamId stream_id);
int ParseClientRtpPort(const std::string& transport);
std::string BasicRealmHeader();
bool DecodeBase64(const std::string& encoded, std::string* decoded);

}  // namespace rtsp_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_PROTOCOL_H_
