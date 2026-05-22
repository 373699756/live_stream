#include "http_connection_state_table.h"

#include "http_protocol.h"

#include <algorithm>
#include <utility>

namespace live_stream {
namespace {

HttpConnectionParseFailure FailureFromRawParseStatus(RawParseStatus status) {
  if (status == RawParseStatus::kPayloadTooLarge) {
    return HttpConnectionParseFailure::kPayloadTooLarge;
  }
  return HttpConnectionParseFailure::kBadRequest;
}

}  // namespace

void HttpConnectionStateTable::Add(ConnectionId connection_id, std::string client_ip) {
  HttpConnectionState session;
  session.client_ip = std::move(client_ip);
  connections_[connection_id] = session;
}

void HttpConnectionStateTable::Clear() { connections_.clear(); }

size_t HttpConnectionStateTable::Size() const { return connections_.size(); }

std::vector<ConnectionId> HttpConnectionStateTable::ConnectionIds() const {
  std::vector<ConnectionId> connection_ids;
  connection_ids.reserve(connections_.size());
  for (const auto &item : connections_) {
    connection_ids.push_back(item.first);
  }
  return connection_ids;
}

bool HttpConnectionStateTable::AppendRequestBytes(ConnectionId connection_id,
                                          const uint8_t *data, uint32_t size) {
  if (data == nullptr) {
    return false;
  }
  auto iter = connections_.find(connection_id);
  if (iter == connections_.end() || iter->second.closing ||
      iter->second.streaming) {
    return false;
  }
  ++iter->second.timeout_generation;
  iter->second.recv_buffer.append(reinterpret_cast<const char *>(data), size);
  return true;
}

HttpConnectionParseResult HttpConnectionStateTable::ParsePendingRequests(
    ConnectionId connection_id, const HttpConnectionParseOptions &options,
    std::vector<HttpConnectionRequestLog> *request_logs) {
  auto iter = connections_.find(connection_id);
  if (iter == connections_.end()) {
    return HttpConnectionParseResult{};
  }
  return ParsePendingRequests(iter, options, request_logs);
}

bool HttpConnectionStateTable::TakeNextRequest(ConnectionId connection_id,
                                       PendingHttpRequest *pending) {
  if (pending == nullptr) {
    return false;
  }
  auto iter = connections_.find(connection_id);
  if (iter == connections_.end() || iter->second.processing ||
      iter->second.pending_requests.empty()) {
    return false;
  }
  *pending = std::move(iter->second.pending_requests.front());
  iter->second.pending_requests.pop_front();
  iter->second.processing = true;
  return true;
}

HttpConnectionParseResult HttpConnectionStateTable::CompleteKeepAliveRequest(
    ConnectionId connection_id, const HttpConnectionParseOptions &options,
    std::vector<HttpConnectionRequestLog> *request_logs) {
  auto iter = connections_.find(connection_id);
  if (iter == connections_.end()) {
    return HttpConnectionParseResult{};
  }
  iter->second.processing = false;
  return ParsePendingRequests(iter, options, request_logs);
}

bool HttpConnectionStateTable::BeginStream(
    ConnectionId connection_id, const std::shared_ptr<void> &stream_owner) {
  auto iter = connections_.find(connection_id);
  if (iter == connections_.end()) {
    return false;
  }
  iter->second.processing = false;
  iter->second.closing = true;
  iter->second.streaming = true;
  iter->second.stream_client_id = 0;
  iter->second.stream_owner = stream_owner;
  iter->second.pending_requests.clear();
  iter->second.recv_buffer.clear();
  ++iter->second.timeout_generation;
  return true;
}

bool HttpConnectionStateTable::AttachStreamClient(ConnectionId connection_id,
                                             HttpStreamClientId client_id) {
  auto iter = connections_.find(connection_id);
  if (iter == connections_.end() || !iter->second.streaming) {
    return false;
  }
  iter->second.stream_client_id = client_id;
  return true;
}

bool HttpConnectionStateTable::IsStreaming(ConnectionId connection_id) const {
  auto iter = connections_.find(connection_id);
  return iter != connections_.end() && iter->second.streaming;
}

bool HttpConnectionStateTable::ArmTimer(ConnectionId connection_id,
                                uint64_t *generation) {
  if (generation == nullptr) {
    return false;
  }
  auto iter = connections_.find(connection_id);
  if (iter == connections_.end()) {
    return false;
  }
  *generation = ++iter->second.timeout_generation;
  return true;
}

bool HttpConnectionStateTable::IsTimerCurrent(ConnectionId connection_id,
                                      uint64_t generation) const {
  auto iter = connections_.find(connection_id);
  return iter != connections_.end() &&
         iter->second.timeout_generation == generation;
}

ClosedHttpConnectionInfo HttpConnectionStateTable::Remove(ConnectionId connection_id) {
  ClosedHttpConnectionInfo closed;
  auto iter = connections_.find(connection_id);
  if (iter == connections_.end()) {
    return closed;
  }
  closed.found = true;
  closed.was_streaming = iter->second.streaming;
  closed.stream_client_id = TakeStreamClient(&iter->second);
  connections_.erase(iter);
  return closed;
}

std::vector<HttpStreamClientId> HttpConnectionStateTable::TakeAllStreamClients() {
  std::vector<HttpStreamClientId> client_ids;
  for (auto &item : connections_) {
    const HttpStreamClientId client_id = TakeStreamClient(&item.second);
    if (client_id != 0) {
      client_ids.push_back(client_id);
    }
  }
  return client_ids;
}

HttpStreamClientId HttpConnectionStateTable::TakeStreamClient(
    HttpConnectionState *session) {
  if (session == nullptr || session->stream_client_id == 0) {
    return 0;
  }
  const HttpStreamClientId client_id = session->stream_client_id;
  session->stream_client_id = 0;
  session->stream_owner.reset();
  return client_id;
}

HttpConnectionParseResult HttpConnectionStateTable::ParsePendingRequests(
    ConnectionMap::iterator iter, const HttpConnectionParseOptions &options,
    std::vector<HttpConnectionRequestLog> *request_logs) {
  HttpConnectionParseResult result;
  result.found = true;
  HttpConnectionState &session = iter->second;
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
      result.failure = HttpConnectionParseFailure::kPayloadTooLarge;
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
      HttpConnectionRequestLog log;
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
