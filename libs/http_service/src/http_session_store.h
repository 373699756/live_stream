#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SESSION_STORE_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SESSION_STORE_H_

#include "http_service.h"
#include "net_service.h"
#include "stream_hub_service.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

struct PendingHttpRequest {
  HttpRequest request;
  bool close_after_response = true;
};

struct ClosedHttpSessionInfo {
  StreamFlvClientId flv_client_id = 0;
  bool was_streaming = false;
  bool found = false;
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

struct HttpSessionRequestLog {
  ConnectionId connection_id = 0;
  std::string client_ip;
  HttpMethod method = HttpMethod::kGet;
  std::string path;
  size_t query_size = 0;
  size_t body_size = 0;
};

struct HttpSessionParseResult {
  bool found = false;
  bool success = true;
  bool has_pending = false;
  HttpSessionParseFailure failure = HttpSessionParseFailure::kNone;
};

// HttpSessionStore owns per-connection HTTP state. It is intentionally not
// internally synchronized; HttpServiceImpl protects it with its service mutex.
class HttpSessionStore {
 public:
  void Add(ConnectionId connection_id, std::string client_ip);
  void Clear();
  size_t Size() const;
  std::vector<ConnectionId> ConnectionIds() const;

  bool AppendRequestBytes(ConnectionId connection_id, const uint8_t *data,
                          uint32_t size);
  HttpSessionParseResult ParsePendingRequests(
      ConnectionId connection_id, const HttpSessionParseOptions &options,
      std::vector<HttpSessionRequestLog> *request_logs);
  bool TakeNextRequest(ConnectionId connection_id, PendingHttpRequest *pending);
  HttpSessionParseResult CompleteKeepAliveRequest(
      ConnectionId connection_id, const HttpSessionParseOptions &options,
      std::vector<HttpSessionRequestLog> *request_logs);

  bool BeginFlvStream(ConnectionId connection_id,
                      const std::shared_ptr<IStreamFlvSink> &sink);
  bool AttachFlvClient(ConnectionId connection_id,
                       StreamFlvClientId client_id);
  bool IsStreaming(ConnectionId connection_id) const;

  bool ArmTimer(ConnectionId connection_id, uint64_t *generation);
  bool IsTimerCurrent(ConnectionId connection_id, uint64_t generation) const;

  ClosedHttpSessionInfo Remove(ConnectionId connection_id);
  std::vector<StreamFlvClientId> TakeAllFlvClients();

 private:
  struct HttpSession {
    std::string recv_buffer;
    std::string client_ip;
    std::deque<PendingHttpRequest> pending_requests;
    uint64_t request_count = 0;
    uint64_t timeout_generation = 0;
    StreamFlvClientId flv_client_id = 0;
    std::shared_ptr<IStreamFlvSink> flv_sink;
    bool processing = false;
    bool closing = false;
    bool streaming = false;
  };

  using SessionMap = std::map<ConnectionId, HttpSession>;

  static StreamFlvClientId TakeFlvClient(HttpSession *session);
  HttpSessionParseResult ParsePendingRequests(
      SessionMap::iterator iter, const HttpSessionParseOptions &options,
      std::vector<HttpSessionRequestLog> *request_logs);

  SessionMap sessions_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_SESSION_STORE_H_
