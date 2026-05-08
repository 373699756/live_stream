#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_STATIC_FILES_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_STATIC_FILES_H_

#include "http_service.h"

#include <string>

namespace live_stream {

enum class StaticFileStatus {
  kOk,
  kNotFound,
  kForbidden,
};

struct StaticFileResult {
  StaticFileStatus status = StaticFileStatus::kNotFound;
  HttpResponse response;
};

StaticFileResult BuildStaticFileResponse(const HttpRequest &request,
                                         const std::string &static_root);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_STATIC_FILES_H_
