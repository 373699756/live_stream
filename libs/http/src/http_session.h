#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SESSION_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SESSION_H_

#include "http_request_splitter.h"
#include "http_media_writer.h"
#include "net.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace live_stream {

struct PendingHttpRequest {
  HttpRequest request;
  bool close_after_response = true;
};

enum class HttpSessionParseFailure {
  kNone,
  kBadRequest,
  kPayloadTooLarge,
};

struct HttpSessionParseOptions {
  uint32_t max_request_header_bytes = 0;
  uint32_t max_request_body_bytes = 0;
  uint32_t max_pipelined_requests = 0;
  uint32_t max_requests_per_connection = 0;
  bool enable_keep_alive = false;
};

struct HttpRequestLog {
  ConnectionId connection_id = 0;
  std::string client_ip;
  HttpMethod method = HttpMethod::kGet;
  std::string path;
  size_t query_size = 0;
  size_t body_size = 0;
};

struct HttpSessionParseResult {
  bool success = true;
  bool has_pending = false;
  HttpSessionParseFailure failure = HttpSessionParseFailure::kNone;
};

struct ClosedHttpSessionInfo {
  HttpMediaClientHandle media_client;
  bool was_streaming = false;
};

class HttpSession {
 public:
  HttpSession(ConnectionId connection_id, std::string client_ip);

  ConnectionId connection_id() const;
  const std::string &client_ip() const;
  bool is_streaming() const;

  bool AppendRequestBytes(const uint8_t *data, uint32_t size);
  HttpSessionParseResult ParsePendingRequests(
      const HttpSessionParseOptions &options,
      std::vector<HttpRequestLog> *request_logs);
  bool TakeNextRequest(PendingHttpRequest *pending);
  HttpSessionParseResult CompleteKeepAliveRequest(
      const HttpSessionParseOptions &options,
      std::vector<HttpRequestLog> *request_logs);

  bool BeginStream();
  bool AttachStreamClient(HttpMediaClientHandle client);
  bool ArmTimer(uint64_t *generation);
  bool IsTimerCurrent(uint64_t generation) const;
  ClosedHttpSessionInfo Close();
  HttpMediaClientHandle TakeMediaClient();

 private:
  static HttpSessionParseFailure FailureFromSplitStatus(
      HttpRequestSplitStatus status);

  ConnectionId connection_id_ = 0;
  std::string client_ip_;
  HttpRequestSplitter splitter_;
  std::deque<PendingHttpRequest> pending_requests_;
  uint64_t request_count_ = 0;
  uint64_t timeout_generation_ = 0;
  HttpMediaClientHandle media_client_;
  bool processing_ = false;
  bool closing_ = false;
  bool streaming_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SESSION_H_
