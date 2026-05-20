#include "http_static_files.h"

#include "http_protocol.h"
#include "infra/fs.h"

#include <string>
#include <vector>

namespace live_stream {
namespace {

std::string ContentTypeForPath(const std::string &path) {
    const std::string lower_path = ToLower(path);
    if (lower_path.size() >= 5 &&
        lower_path.substr(lower_path.size() - 5) == ".html") {
        return "text/html";
    }
    if (lower_path.size() >= 4 &&
        lower_path.substr(lower_path.size() - 4) == ".css") {
        return "text/css";
    }
    if (lower_path.size() >= 3 &&
        lower_path.substr(lower_path.size() - 3) == ".js") {
        return "application/javascript";
    }
    if (lower_path.size() >= 4 &&
        lower_path.substr(lower_path.size() - 4) == ".jpg") {
        return "image/jpeg";
    }
    if (lower_path.size() >= 4 &&
        lower_path.substr(lower_path.size() - 4) == ".png") {
        return "image/png";
    }
    return "application/octet-stream";
}

bool IsUnsafeStaticPath(const std::string &path) {
    const std::string lower_path = ToLower(path);
    return path.find("..") != std::string::npos ||
           path.find('\\') != std::string::npos ||
           lower_path.find("%2e") != std::string::npos ||
           lower_path.find("%5c") != std::string::npos;
}

void AddStaticCacheHeaders(const std::string &relative,
                           HttpResponse *response) {
    if (!response) {
        return;
    }
    if (relative == "index.html" || relative.rfind("assets/", 0) == 0 ||
        relative.rfind("vendor/", 0) == 0) {
        response->headers["Cache-Control"] =
            "no-cache, no-store, must-revalidate";
        response->headers["Pragma"] = "no-cache";
        response->headers["Expires"] = "0";
    }
}

}  // namespace

StaticFileResult BuildStaticFileResponse(const HttpRequest &request,
                                         const std::string &static_root) {
    StaticFileResult result;
    if (static_root.empty()) {
        result.status = StaticFileStatus::kNotFound;
        return result;
    }
    if (IsUnsafeStaticPath(request.path)) {
        result.status = StaticFileStatus::kForbidden;
        return result;
    }
    std::string relative =
        request.path == "/" ? "index.html" : request.path.substr(1);
    if (relative.empty()) {
        relative = "index.html";
    }
    const std::string path = infra::Path::Join(static_root, relative);
    result.relative_path = relative;
    result.path = path;
    std::string content = infra::File::ReadAll(path);
    if (content.empty()) {
        result.status = StaticFileStatus::kNotFound;
        return result;
    }
    result.status = StaticFileStatus::kOk;
    result.response.status_code = 200;
    result.response.headers["Content-Type"] = ContentTypeForPath(path);
    AddStaticCacheHeaders(relative, &result.response);
    result.response.body = content;
    return result;
}

std::vector<StaticAssetStatus> CheckStaticAssets(
    const std::string &static_root,
    const std::vector<std::string> &relative_paths) {
    std::vector<StaticAssetStatus> assets;
    assets.reserve(relative_paths.size());
    for (const std::string &relative_path : relative_paths) {
        StaticAssetStatus asset;
        asset.relative_path = relative_path;
        asset.path = infra::Path::Join(static_root, relative_path);
        asset.exists = infra::File::Exists(asset.path);
        asset.size = asset.exists ? infra::File::Size(asset.path) : 0;
        assets.push_back(asset);
    }
    return assets;
}

}  // namespace live_stream
