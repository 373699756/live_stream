#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_CONNECTION_STATE_TABLE_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_CONNECTION_STATE_TABLE_H_

#include "http_stream_writer.h"
#include "http_service.h"
#include "net_service.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace live_stream {

struct PendingHttpRequest {
  HttpRequest request;
  bool close_after_response = true;
};

struct ClosedHttpConnectionInfo {
  HttpStreamClientId stream_client_id = 0;
  bool was_streaming = false;
  bool found = false;
};

enum class HttpConnectionParseFailure {
  kNone,
  kBadRequest,
  kPayloadTooLarge,
};

struct HttpConnectionParseOptions {
  uint32_t max_request_header_bytes = 0;
  uint32_t max_request_body_bytes = 0;
  uint32_t max_pipelined_requests = 0;
  uint32_t max_requests_per_connection = 0;
  bool enable_keep_alive = false;
};

struct HttpConnectionRequestLog {
  ConnectionId connection_id = 0;
  std::string client_ip;
  HttpMethod method = HttpMethod::kGet;
  std::string path;
  size_t query_size = 0;
  size_t body_size = 0;
};

struct HttpConnectionParseResult {
  bool found = false;
  bool success = true;
  bool has_pending = false;
  HttpConnectionParseFailure failure = HttpConnectionParseFailure::kNone;
};

// HttpConnectionStateTable owns per-connection HTTP state. It is intentionally
// not internally synchronized; HttpServer protects it with its mutex.
class HttpConnectionStateTable {
 public:
  void Add(ConnectionId connection_id, std::string client_ip);
  void Clear();
  size_t Size() const;
  std::vector<ConnectionId> ConnectionIds() const;

  bool AppendRequestBytes(ConnectionId connection_id, const uint8_t *data,
                          uint32_t size);
  HttpConnectionParseResult ParsePendingRequests(
      ConnectionId connection_id, const HttpConnectionParseOptions &options,
      std::vector<HttpConnectionRequestLog> *request_logs);
  bool TakeNextRequest(ConnectionId connection_id, PendingHttpRequest *pending);
  HttpConnectionParseResult CompleteKeepAliveRequest(
      ConnectionId connection_id, const HttpConnectionParseOptions &options,
      std::vector<HttpConnectionRequestLog> *request_logs);

  bool BeginStream(ConnectionId connection_id);
  bool AttachStreamClient(ConnectionId connection_id,
                          HttpStreamClientId client_id);
  bool IsStreaming(ConnectionId connection_id) const;

  bool ArmTimer(ConnectionId connection_id, uint64_t *generation);
  bool IsTimerCurrent(ConnectionId connection_id, uint64_t generation) const;

  ClosedHttpConnectionInfo Remove(ConnectionId connection_id);
  std::vector<HttpStreamClientId> TakeAllStreamClients();

 private:
  struct HttpConnectionState {
    std::string recv_buffer;
    std::string client_ip;
    std::deque<PendingHttpRequest> pending_requests;
    uint64_t request_count = 0;
    uint64_t timeout_generation = 0;
    HttpStreamClientId stream_client_id = 0;
    bool processing = false;
    bool closing = false;
    bool streaming = false;
  };

  using ConnectionMap = std::map<ConnectionId, HttpConnectionState>;

  static HttpStreamClientId TakeStreamClient(HttpConnectionState *session);
  HttpConnectionParseResult ParsePendingRequests(
      ConnectionMap::iterator iter, const HttpConnectionParseOptions &options,
      std::vector<HttpConnectionRequestLog> *request_logs);

  ConnectionMap connections_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_CONNECTION_STATE_TABLE_H_
