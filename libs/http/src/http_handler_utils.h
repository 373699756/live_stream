#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_HANDLER_UTILS_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_HANDLER_UTILS_H_

#include "http_access.h"
#include "http.h"
#include "config_json.h"
#include "media/stream_types.h"

#include <map>
#include <string>

namespace live_stream {

class IAiView;
class IConfig;
class DeviceMedia;

constexpr const char *kHttpModuleName = "http";

enum class HttpErrorCode {
    kInvalidArgument,
    kUnauthenticated,
    kPermissionDenied,
    kStreamNotFound,
    kProtocolUnavailable,
    kPeerNotFound,
    kResourceBusy,
    kInternalError,
};

HttpResponse JsonResponse(int status_code, const ConfigJson &value);
HttpResponse JsonEnvelopeResponse(int status_code, const ConfigJson &data,
                                  const std::string &request_id);
HttpResponse ErrorResponse(int status_code, HttpErrorCode code,
                           const std::string &message);
HttpResponse ErrorEnvelopeResponse(int status_code, HttpErrorCode code,
                                   const std::string &message,
                                   const std::string &request_id);
HttpResponse StatusResponse(int status_code, const std::string &reason);
HttpResponse ForbiddenResponse(const AuthPrincipal &principal);
HttpResponse OkResponse();
HttpResponse AddJsonEnvelope(const HttpRequest &request,
                             const HttpResponse &response);
std::string RequestIdForResponse(const HttpRequest &request);
const char *HttpErrorCodeName(HttpErrorCode code);
HttpErrorCode HttpErrorCodeForStatus(int status_code);
bool RequireAuth(HttpAccess *access, const HttpRequest &request,
                 AuthPrincipal *principal);
HttpResponse RequireAuthResponse(HttpAccess *access,
                                 const HttpRequest &request,
                                 AuthPrincipal *principal);
bool RequirePermissionOrForbidden(HttpAccess *access,
                                  const HttpRequest &request,
                                  AuthPermission permission,
                                  const std::string &target,
                                  AuthPrincipal *principal);
bool ParseJsonObject(const HttpRequest &request, ConfigJson *body);
bool ParseOptionalJsonObject(const HttpRequest &request, ConfigJson *body);
bool IsMediaRestarting(DeviceMedia *device);
std::string PathSuffix(const std::string &path, const std::string &prefix);
std::string BuildStreamingHeaderBlock(
    int status_code, const std::map<std::string, std::string> &headers);
bool StartsWith(const std::string &value, const std::string &prefix);
bool IsAiConfigEnabled(IConfig *config);
bool IsAiHealthy(const IAiView *ai);
const char *StreamIdToJsonString(StreamId stream_id);
bool StreamIdFromJsonString(const std::string &value, StreamId *stream_id);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_HANDLER_UTILS_H_
