#ifndef LIVE_STREAM_HTTP_SRC_HTTP_REQUEST_ID_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_REQUEST_ID_H_

#include <cstdint>
#include <string>

namespace live_stream {

std::string MakeRequestId(uint64_t id);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_REQUEST_ID_H_
