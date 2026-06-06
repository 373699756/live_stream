#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_REQUEST_SPLITTER_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_REQUEST_SPLITTER_H_

#include "http_protocol.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {

enum class HttpRequestSplitStatus {
  kComplete,
  kIncomplete,
  kBadRequest,
  kPayloadTooLarge,
};

struct HttpRequestSplitOptions {
  uint32_t max_header_bytes = 0;
  uint32_t max_body_bytes = 0;
};

struct HttpRequestSplitResult {
  HttpRequestSplitStatus status = HttpRequestSplitStatus::kIncomplete;
  HttpRequest request;
  bool keep_alive = false;
};

class HttpRequestSplitter {
 public:
  bool Append(const uint8_t *data, uint32_t size);
  void Clear();
  size_t BufferedBytes() const;

  HttpRequestSplitResult SplitNext(const HttpRequestSplitOptions &options,
                                   const std::string &client_ip);

 private:
  std::string recv_buffer_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_REQUEST_SPLITTER_H_
