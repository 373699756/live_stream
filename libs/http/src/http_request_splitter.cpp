#include "http_request_splitter.h"

#include <cstdlib>
#include <string>

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

std::string LowerAscii(std::string value) {
    for (char &c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

std::string TrimAscii(const std::string &value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin &&
           (value[end - 1] == ' ' || value[end - 1] == '\t')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool HeaderValue(const std::string &header_block,
                 const std::string &name,
                 std::string *value) {
    if (value == nullptr) {
        return false;
    }
    const std::string lower_name = LowerAscii(name);
    std::size_t pos = 0;
    while (pos < header_block.size()) {
        const std::size_t line_end = header_block.find("\r\n", pos);
        const std::string line =
            line_end == std::string::npos
                ? header_block.substr(pos)
                : header_block.substr(pos, line_end - pos);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos &&
            LowerAscii(TrimAscii(line.substr(0, colon))) == lower_name) {
            *value = TrimAscii(line.substr(colon + 1));
            return true;
        }
        if (line_end == std::string::npos) {
            break;
        }
        pos = line_end + 2;
    }
    return false;
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
    std::string().swap(recv_buffer_);
}

size_t HttpRequestSplitter::BufferedBytes() const {
    return recv_buffer_.size();
}

bool HttpRequestSplitter::ShouldSendContinue(
    uint32_t max_header_bytes,
    uint32_t max_body_bytes) const {
    const size_t header_end = recv_buffer_.find("\r\n\r\n");
    if (header_end == std::string::npos ||
        header_end > static_cast<size_t>(max_header_bytes)) {
        return false;
    }

    const std::string header_block = recv_buffer_.substr(0, header_end);
    std::string expect;
    if (!HeaderValue(header_block, "Expect", &expect) ||
        LowerAscii(expect).find("100-continue") == std::string::npos) {
        return false;
    }

    std::string content_length_text;
    if (!HeaderValue(header_block, "Content-Length", &content_length_text)) {
        return false;
    }
    char *end = nullptr;
    const unsigned long parsed =
        std::strtoul(content_length_text.c_str(), &end, 10);
    if (end == content_length_text.c_str() || *end != '\0' ||
        parsed == 0 || parsed > max_body_bytes) {
        return false;
    }
    const size_t received_body_size = recv_buffer_.size() - header_end - 4;
    return received_body_size < static_cast<size_t>(parsed);
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
    if (recv_buffer_.empty()) {
        std::string().swap(recv_buffer_);
    }
    // erase 后剩余字节可能已经是下一个 pipeline request 的开头，
    // HttpSession 会按 max_pipelined_requests 控制本轮继续解析多少个。
    result.request = std::move(parsed.request);
    result.keep_alive = parsed.keep_alive;
    return result;
}

}  // namespace live_stream
