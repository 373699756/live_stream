#include "http_request_splitter.h"

namespace live_stream {

namespace {

HttpRequestSplitStatus SplitStatusFromRawStatus(RawParseStatus status) {
    switch (status) {
        case RawParseStatus::kComplete:
            return HttpRequestSplitStatus::kComplete;
        case RawParseStatus::kIncomplete:
            return HttpRequestSplitStatus::kIncomplete;
        case RawParseStatus::kPayloadTooLarge:
            return HttpRequestSplitStatus::kPayloadTooLarge;
        case RawParseStatus::kBadRequest:
            return HttpRequestSplitStatus::kBadRequest;
    }
    return HttpRequestSplitStatus::kBadRequest;
}

}  // namespace

bool HttpRequestSplitter::Append(const uint8_t *data, uint32_t size) {
    if (data == nullptr) {
        return false;
    }
    // recv_buffer_ 允许包含多个 pipeline request，也允许只包含半包；
    // 大小上限在 SplitNext() 中统一按 header+body 边界判断。
    recv_buffer_.append(reinterpret_cast<const char *>(data), size);
    return true;
}

void HttpRequestSplitter::Clear() {
    recv_buffer_.clear();
}

size_t HttpRequestSplitter::BufferedBytes() const {
    return recv_buffer_.size();
}

HttpRequestSplitResult HttpRequestSplitter::SplitNext(
    const HttpRequestSplitOptions &options, const std::string &client_ip) {
    HttpRequestSplitResult result;
    const size_t max_buffer_size =
        static_cast<size_t>(options.max_header_bytes) + 4 +
        options.max_body_bytes;
    if (recv_buffer_.empty()) {
        result.status = HttpRequestSplitStatus::kIncomplete;
        return result;
    }
    if (recv_buffer_.size() > max_buffer_size) {
        // 请求头还没完整时也要受总上限约束，避免恶意客户端一直发无结束符头部。
        result.status = HttpRequestSplitStatus::kPayloadTooLarge;
        return result;
    }

    // ParseRawRequest 只消费一个完整 HTTP message；剩余字节留在 recv_buffer_，
    // 由 HttpSession 的 pipeline 上限控制继续排队。
    RawParseResult parsed = ParseRawRequest(
        recv_buffer_, options.max_header_bytes, options.max_body_bytes,
        client_ip);
    result.status = SplitStatusFromRawStatus(parsed.status);
    if (result.status != HttpRequestSplitStatus::kComplete) {
        return result;
    }
    if (parsed.consumed_bytes == 0 || parsed.consumed_bytes > recv_buffer_.size()) {
        result.status = HttpRequestSplitStatus::kBadRequest;
        return result;
    }

    recv_buffer_.erase(0, parsed.consumed_bytes);
    // erase 后剩余字节可能已经是下一个 pipeline request 的开头，
    // HttpSession 会按 max_pipelined_requests 控制本轮继续解析多少个。
    result.request = std::move(parsed.request);
    result.keep_alive = parsed.keep_alive;
    return result;
}

}  // namespace live_stream
