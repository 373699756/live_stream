#ifndef LIVE_STREAM_DEVICE_SRC_CONFIG_ERROR_H_
#define LIVE_STREAM_DEVICE_SRC_CONFIG_ERROR_H_

#include "config.h"

#include <string>

namespace live_stream {
namespace media_internal {

inline ConfigCode MakeConfigError(const std::string &field,
                                  const std::string &msg,
                                  ConfigError *error) {
    if (error != nullptr) {
        error->field = field;
        error->message = msg;
    }
    return ConfigCode::kVerify;
}

inline std::string JoinField(const std::string &parent, const char *child) {
    if (parent.empty()) {
        return child != nullptr ? child : "";
    }
    return child != nullptr ? (parent + "." + child) : parent;
}

inline std::string JoinField(const std::string &parent,
                             const std::string &child) {
    return JoinField(parent, child.c_str());
}

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_CONFIG_ERROR_H_
