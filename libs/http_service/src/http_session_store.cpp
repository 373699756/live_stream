#include "http_session_store.h"

#include "http_protocol.h"

#include <algorithm>
#include <utility>

namespace live_stream {
namespace {

HttpSessionParseFailure FailureFromRawParseStatus(RawParseStatus status) {
  if (status == RawParseStatus::kPayloadTooLarge) {
    return HttpSessionParseFailure::kPayloadTooLarge;
  }
  return HttpSessionParseFailure::kBadRequest;
}

}  // namespace

void HttpSessionStore::Add(ConnectionId connection_id, std::string client_ip) {
  HttpSession session;
  session.client_ip = std::move(client_ip);
  sessions_[connection_id] = session;
}

void HttpSessionStore::Clear() { sessions_.clear(); }

size_t HttpSessionStore::Size() const { return sessions_.size(); }

std::vector<ConnectionId> HttpSessionStore::ConnectionIds() const {
  std::vector<ConnectionId> connection_ids;
  connection_ids.reserve(sessions_.size());
  for (const auto &item : sessions_) {
    connection_ids.push_back(item.first);
  }
  return connection_ids;
}

bool HttpSessionStore::AppendRequestBytes(ConnectionId connection_id,
                                          const uint8_t *data, uint32_t size) {
  if (data == nullptr) {
    return false;
  }
  auto iter = sessions_.find(connection_id);
  if (iter == sessions_.end() || iter->second.closing ||
      iter->second.streaming) {
    return false;
  }
  ++iter->second.timeout_generation;
  iter->second.recv_buffer.append(reinterpret_cast<const char *>(data), size);
  return true;
}

HttpSessionParseResult HttpSessionStore::ParsePendingRequests(
    ConnectionId connection_id, const HttpSessionParseOptions &options,
    std::vector<HttpSessionRequestLog> *request_logs) {
  auto iter = sessions_.find(connection_id);
  if (iter == sessions_.end()) {
    return HttpSessionParseResult{};
  }
  return ParsePendingRequests(iter, options, request_logs);
}

bool HttpSessionStore::TakeNextRequest(ConnectionId connection_id,
                                       PendingHttpRequest *pending) {
  if (pending == nullptr) {
    return false;
  }
  auto iter = sessions_.find(connection_id);
  if (iter == sessions_.end() || iter->second.processing ||
      iter->second.pending_requests.empty()) {
    return false;
  }
  *pending = std::move(iter->second.pending_requests.front());
  iter->second.pending_requests.pop_front();
  iter->second.processing = true;
  return true;
}

HttpSessionParseResult HttpSessionStore::CompleteKeepAliveRequest(
    ConnectionId connection_id, const HttpSessionParseOptions &options,
    std::vector<HttpSessionRequestLog> *request_logs) {
  auto iter = sessions_.find(connection_id);
  if (iter == sessions_.end()) {
    return HttpSessionParseResult{};
  }
  iter->second.processing = false;
  return ParsePendingRequests(iter, options, request_logs);
}

bool HttpSessionStore::BeginFlvStream(
    ConnectionId connection_id, const std::shared_ptr<IStreamFlvSink> &sink) {
  auto iter = sessions_.find(connection_id);
  if (iter == sessions_.end()) {
    return false;
  }
  iter->second.processing = false;
  iter->second.closing = true;
  iter->second.streaming = true;
  iter->second.flv_client_id = 0;
  iter->second.flv_sink = sink;
  iter->second.pending_requests.clear();
  iter->second.recv_buffer.clear();
  ++iter->second.timeout_generation;
  return true;
}

bool HttpSessionStore::AttachFlvClient(ConnectionId connection_id,
                                       StreamFlvClientId client_id) {
  auto iter = sessions_.find(connection_id);
  if (iter == sessions_.end() || !iter->second.streaming) {
    return false;
  }
  iter->second.flv_client_id = client_id;
  return true;
}

bool HttpSessionStore::IsStreaming(ConnectionId connection_id) const {
  auto iter = sessions_.find(connection_id);
  return iter != sessions_.end() && iter->second.streaming;
}

bool HttpSessionStore::ArmTimer(ConnectionId connection_id,
                                uint64_t *generation) {
  if (generation == nullptr) {
    return false;
  }
  auto iter = sessions_.find(connection_id);
  if (iter == sessions_.end()) {
    return false;
  }
  *generation = ++iter->second.timeout_generation;
  return true;
}

bool HttpSessionStore::IsTimerCurrent(ConnectionId connection_id,
                                      uint64_t generation) const {
  auto iter = sessions_.find(connection_id);
  return iter != sessions_.end() &&
         iter->second.timeout_generation == generation;
}

ClosedHttpSessionInfo HttpSessionStore::Remove(ConnectionId connection_id) {
  ClosedHttpSessionInfo closed;
  auto iter = sessions_.find(connection_id);
  if (iter == sessions_.end()) {
    return closed;
  }
  closed.found = true;
  closed.was_streaming = iter->second.streaming;
  closed.flv_client_id = TakeFlvClient(&iter->second);
  sessions_.erase(iter);
  return closed;
}

std::vector<StreamFlvClientId> HttpSessionStore::TakeAllFlvClients() {
  std::vector<StreamFlvClientId> client_ids;
  for (auto &item : sessions_) {
    const StreamFlvClientId client_id = TakeFlvClient(&item.second);
    if (client_id != 0) {
      client_ids.push_back(client_id);
    }
  }
  return client_ids;
}

StreamFlvClientId HttpSessionStore::TakeFlvClient(HttpSession *session) {
  if (session == nullptr || session->flv_client_id == 0) {
    return 0;
  }
  const StreamFlvClientId client_id = session->flv_client_id;
  session->flv_client_id = 0;
  session->flv_sink.reset();
  return client_id;
}

HttpSessionParseResult HttpSessionStore::ParsePendingRequests(
    SessionMap::iterator iter, const HttpSessionParseOptions &options,
    std::vector<HttpSessionRequestLog> *request_logs) {
  HttpSessionParseResult result;
  result.found = true;
  HttpSession &session = iter->second;
  const size_t max_buffer_size =
      static_cast<size_t>(options.max_request_header_bytes) + 4 +
      options.max_request_body_bytes;
  size_t parsed_count = 0;
  while (!session.closing &&
         parsed_count < static_cast<size_t>(options.max_pipelined_requests)) {
    if (session.recv_buffer.empty()) {
      result.has_pending = !session.pending_requests.empty();
      return result;
    }
    if (session.recv_buffer.size() > max_buffer_size) {
      session.closing = true;
      result.success = false;
      result.failure = HttpSessionParseFailure::kPayloadTooLarge;
      result.has_pending = !session.pending_requests.empty();
      return result;
    }
    RawParseResult parsed =
        ParseRawRequest(session.recv_buffer, options.max_request_header_bytes,
                        options.max_request_body_bytes, session.client_ip);
    if (parsed.status == RawParseStatus::kIncomplete) {
      result.has_pending = !session.pending_requests.empty();
      return result;
    }
    if (parsed.status != RawParseStatus::kComplete ||
        parsed.consumed_bytes == 0 ||
        parsed.consumed_bytes > session.recv_buffer.size()) {
      session.closing = true;
      result.success = false;
      result.failure = FailureFromRawParseStatus(parsed.status);
      result.has_pending = !session.pending_requests.empty();
      return result;
    }

    session.recv_buffer.erase(0, parsed.consumed_bytes);
    ++session.request_count;
    PendingHttpRequest pending;
    pending.request = std::move(parsed.request);
    pending.close_after_response =
        !options.enable_keep_alive || !parsed.keep_alive ||
        session.request_count >= options.max_requests_per_connection;
    session.pending_requests.push_back(std::move(pending));
    const PendingHttpRequest &queued = session.pending_requests.back();
    if (request_logs != nullptr) {
      HttpSessionRequestLog log;
      log.connection_id = iter->first;
      log.client_ip = session.client_ip;
      log.method = queued.request.method;
      log.path = queued.request.path;
      log.query_size = queued.request.query_string.size();
      log.body_size = queued.request.body.size();
      request_logs->push_back(std::move(log));
    }
    ++parsed_count;
    if (session.pending_requests.back().close_after_response) {
      session.closing = true;
      session.recv_buffer.clear();
      result.has_pending = true;
      return result;
    }
  }
  result.has_pending = !session.pending_requests.empty();
  return result;
}

}  // namespace live_stream
