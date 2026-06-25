#ifndef LIVE_STREAM_HTTP_SRC_HTTP_STREAM_ID_JSON_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_STREAM_ID_JSON_H_

#include "media/stream_types.h"

#include <string>

namespace live_stream {

const char *StreamIdToJsonString(StreamId stream_id);
bool StreamIdFromJsonString(const std::string &value, StreamId *stream_id);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_STREAM_ID_JSON_H_
