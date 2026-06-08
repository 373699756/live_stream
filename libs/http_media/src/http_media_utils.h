#ifndef LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_MEDIA_UTILS_H_
#define LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_MEDIA_UTILS_H_

#include "auth.h"
#include "config_json.h"
#include "http.h"
#include "http_access.h"
#include "media/stream_types.h"

#include <map>
#include <string>

namespace live_stream {

constexpr const char *kHttpMediaModuleName = "http_media";

class IDeviceMedia;

HttpResponse HttpMediaJsonResponse(int status_code, const ConfigJson &value);
HttpResponse HttpMediaStatusResponse(int status_code,
                                     const std::string &reason);
HttpResponse HttpMediaTextResponse(int status_code,
                                   const std::string &reason);
HttpResponse HttpMediaForbiddenResponse(const AuthPrincipal &principal);
HttpResponse HttpMediaOkResponse();
HttpResponse RequireHttpMediaAuthResponse(HttpAccess *access,
                                          const HttpRequest &request,
                                          AuthPrincipal *principal);
HttpResponse RequireHttpMediaPlaybackAuthResponse(HttpAccess *access,
                                                  const HttpRequest &request,
                                                  AuthPrincipal *principal);
bool ParseHttpMediaOptionalJsonObject(const HttpRequest &request,
                                      ConfigJson *body);
bool IsHttpMediaRestarting(IDeviceMedia *device_media);
std::string HttpMediaPathSuffix(const std::string &path,
                                const std::string &prefix);
std::string BuildHttpMediaStreamingHeaderBlock(
    int status_code, const std::map<std::string, std::string> &headers);
bool HttpMediaStartsWith(const std::string &value,
                         const std::string &prefix);
const char *HttpMediaStreamIdToJsonString(StreamId stream_id);
bool HttpMediaStreamIdFromJsonString(const std::string &value,
                                     StreamId *stream_id);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_MEDIA_UTILS_H_
