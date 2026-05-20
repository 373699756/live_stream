#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_STATIC_FILES_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_STATIC_FILES_H_

#include "http_service.h"

#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {

enum class StaticFileStatus {
    kOk,
    kNotFound,
    kForbidden,
};

struct StaticFileResult {
    StaticFileStatus status = StaticFileStatus::kNotFound;
    std::string path;
    std::string relative_path;
    HttpResponse response;
};

struct StaticAssetStatus {
    std::string relative_path;
    std::string path;
    bool exists = false;
    uint64_t size = 0;
};

StaticFileResult BuildStaticFileResponse(const HttpRequest &request,
                                         const std::string &static_root);
std::vector<StaticAssetStatus> CheckStaticAssets(
    const std::string &static_root,
    const std::vector<std::string> &relative_paths);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_STATIC_FILES_H_
