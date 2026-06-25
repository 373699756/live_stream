#ifndef LIVE_STREAM_HTTP_SRC_HTTP_PATH_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_PATH_H_

#include <string>

namespace live_stream {

std::string PathSuffix(const std::string &path, const std::string &prefix);
bool StartsWith(const std::string &value, const std::string &prefix);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_PATH_H_
