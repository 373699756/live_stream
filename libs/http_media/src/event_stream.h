#ifndef LIVE_STREAM_HTTP_MEDIA_SRC_EVENT_STREAM_H_
#define LIVE_STREAM_HTTP_MEDIA_SRC_EVENT_STREAM_H_

#include "event.h"

#include <string>

namespace live_stream {

constexpr const char *kEventStreamPath = "/api/events";

std::string BuildEventStreamMessage(const Event &event);
std::string BuildEventStreamHello();

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_MEDIA_SRC_EVENT_STREAM_H_
