#include "http_media_path.h"

namespace live_stream {

std::string HttpMediaPathSuffix(const std::string &path,
                                const std::string &prefix) {
    if (!HttpMediaStartsWith(path, prefix)) {
        return std::string();
    }
    return path.substr(prefix.size());
}

bool HttpMediaStartsWith(const std::string &value,
                         const std::string &prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

}  // namespace live_stream
