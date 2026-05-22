#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_HANDLER_UTILS_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_HANDLER_UTILS_H_

#include "http_access.h"
#include "http_service.h"
#include "config_json.h"
#include "media/stream_types.h"

#include <map>
#include <string>

namespace live_stream {

class IAiView;
class IConfigService;
class IMediaService;

constexpr const char *kHttpModuleName = "http_service";

HttpResponse JsonResponse(int status_code, const ConfigJson &value);
HttpResponse StatusResponse(int status_code, const std::string &reason);
HttpResponse OkResponse();
bool RequireAuth(HttpAccess *access, const HttpRequest &request,
                 AuthPrincipal *principal);
bool RequirePermissionOrForbidden(HttpAccess *access,
                                  const HttpRequest &request,
                                  AuthPermission permission,
                                  const std::string &target,
                                  AuthPrincipal *principal);
bool ParseJsonObject(const HttpRequest &request, ConfigJson *body);
bool ParseOptionalJsonObject(const HttpRequest &request, ConfigJson *body);
bool IsMediaRestarting(IMediaService *media_service);
std::string PathSuffix(const std::string &path, const std::string &prefix);
std::string BuildStreamingHeaderBlock(
    int status_code, const std::map<std::string, std::string> &headers);
bool StartsWith(const std::string &value, const std::string &prefix);
bool IsAiConfigEnabled(IConfigService *config_service);
bool IsAiServiceHealthy(const IAiView *service);
const char *StreamIdToJsonString(StreamId stream_id);
bool StreamIdFromJsonString(const std::string &value, StreamId *stream_id);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_HANDLER_UTILS_H_
