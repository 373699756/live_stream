#include "http_session.h"

#include <utility>

namespace live_stream {

HttpSession::HttpSession(ConnectionId connection_id, std::string client_ip)
    : connection_id_(connection_id), client_ip_(std::move(client_ip)) {}

ConnectionId HttpSession::connection_id() const {
  return connection_id_;
}

const std::string &HttpSession::client_ip() const {
  return client_ip_;
}

bool HttpSession::is_streaming() const {
  return streaming_;
}

bool HttpSession::AppendRequestBytes(const uint8_t *data, uint32_t size) {
  if (data == nullptr || closing_ || streaming_) {
    return false;
  }
  // 收到新字节就推进 timeout generation，旧请求超时 timer 不能再关闭当前连接。
  ++timeout_generation_;
  return splitter_.Append(data, size);
}

HttpSessionParseResult HttpSession::ParsePendingRequests(
    const HttpSessionParseOptions &options,
    std::vector<HttpRequestLog> *request_logs) {
  HttpSessionParseResult result;
  size_t parsed_count = 0;
  HttpRequestSplitOptions split_options;
  split_options.max_header_bytes = options.max_request_header_bytes;
  split_options.max_body_bytes = options.max_request_body_bytes;

  while (!closing_ &&
         parsed_count < static_cast<size_t>(options.max_pipelined_requests)) {
    if (splitter_.BufferedBytes() == 0) {
      result.has_pending = !pending_requests_.empty();
      return result;
    }

    HttpRequestSplitResult split =
        splitter_.SplitNext(split_options, client_ip_);
    if (split.status == HttpRequestSplitStatus::kIncomplete) {
      result.has_pending = !pending_requests_.empty();
      return result;
    }
    if (split.status != HttpRequestSplitStatus::kComplete) {
      closing_ = true;
      result.success = false;
      result.failure = FailureFromSplitStatus(split.status);
      result.has_pending = !pending_requests_.empty();
      return result;
    }

    // 支持有限 HTTP pipeline：先把已完整请求排队，执行层一次只取一个，
    // 避免同一连接上的多个控制请求并发修改共享业务状态。
    ++request_count_;
    PendingHttpRequest pending;
    pending.request = std::move(split.request);
    pending.close_after_response =
        !options.enable_keep_alive || !split.keep_alive ||
        request_count_ >= options.max_requests_per_connection;
    pending_requests_.push_back(std::move(pending));

    const PendingHttpRequest &queued = pending_requests_.back();
    if (request_logs != nullptr) {
      HttpRequestLog log;
      log.connection_id = connection_id_;
      log.client_ip = client_ip_;
      log.method = queued.request.method;
      log.path = queued.request.path;
      log.query_size = queued.request.query_string.size();
      log.body_size = queued.request.body.size();
      request_logs->push_back(std::move(log));
    }

    ++parsed_count;
    if (pending_requests_.back().close_after_response) {
      closing_ = true;
      splitter_.Clear();
      result.has_pending = true;
      return result;
    }
  }
  result.has_pending = !pending_requests_.empty();
  return result;
}

bool HttpSession::TakeNextRequest(PendingHttpRequest *pending) {
  if (pending == nullptr || processing_ || pending_requests_.empty()) {
    return false;
  }
  // processing_ 从这里置位，到普通响应完成或 BeginStream() 时释放。
  // 这保证同一连接上不会并行执行两个 handler。
  *pending = std::move(pending_requests_.front());
  pending_requests_.pop_front();
  processing_ = true;
  return true;
}

HttpSessionParseResult HttpSession::CompleteKeepAliveRequest(
    const HttpSessionParseOptions &options,
    std::vector<HttpRequestLog> *request_logs) {
  processing_ = false;
  return ParsePendingRequests(options, request_logs);
}

bool HttpSession::BeginStream() {
  // 进入 streaming 后该 TCP 连接不再解析 HTTP request；后续字节视为非法输入，
  // 生命周期由媒体 client 和 TCP close callback 接管。
  processing_ = false;
  closing_ = true;
  streaming_ = true;
  media_client_ = HttpMediaClientHandle{};
  pending_requests_.clear();
  splitter_.Clear();
  ++timeout_generation_;
  return true;
}

bool HttpSession::AttachStreamClient(HttpMediaClientHandle client) {
  if (!streaming_ || client.type == HttpMediaClientType::kNone ||
      client.id == 0) {
    return false;
  }
  // 同一个 HTTP streaming session 只绑定一个媒体 client。切换协议应先关闭
  // 原连接，而不是在一个 TCP session 中替换 client。
  media_client_ = client;
  return true;
}

RenewedHttpSessionTimeout HttpSession::RenewTimeout() {
  RenewedHttpSessionTimeout timeout;
  timeout.replaced_timer_id = timer_id_;
  timer_id_ = 0;
  timeout.generation = ++timeout_generation_;
  return timeout;
}

bool HttpSession::InstallTimeout(uint64_t generation, NetTimerId timer_id) {
  if (timer_id == 0 || timeout_generation_ != generation) {
    return false;
  }
  timer_id_ = timer_id;
  return true;
}

NetTimerId HttpSession::CancelTimeout() {
  const NetTimerId timer_id = timer_id_;
  timer_id_ = 0;
  ++timeout_generation_;
  return timer_id;
}

bool HttpSession::ExpireTimeout(uint64_t generation) {
  if (timeout_generation_ != generation || timer_id_ == 0) {
    return false;
  }
  // 只有当前 generation 的 timer 才能消费 timer_id_ 并触发关闭。
  timer_id_ = 0;
  return true;
}

ClosedHttpSessionInfo HttpSession::Close() {
  ClosedHttpSessionInfo closed;
  closed.was_streaming = streaming_;
  closed.media_client = TakeMediaClient();
  return closed;
}

HttpMediaClientHandle HttpSession::TakeMediaClient() {
  const HttpMediaClientHandle client = media_client_;
  media_client_ = HttpMediaClientHandle{};
  return client;
}

HttpSessionParseFailure HttpSession::FailureFromSplitStatus(
    HttpRequestSplitStatus status) {
  if (status == HttpRequestSplitStatus::kPayloadTooLarge) {
    return HttpSessionParseFailure::kPayloadTooLarge;
  }
  return HttpSessionParseFailure::kBadRequest;
}

}  // namespace live_stream
