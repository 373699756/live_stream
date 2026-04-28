#include "rtsp_protocol.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace live_stream {
namespace rtsp_internal {
namespace {

constexpr uint8_t kPayloadTypeH264 = 96;

std::string RtspStatusText(int status) {
    switch (status) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 454:
            return "Session Not Found";
        case 455:
            return "Method Not Valid in This State";
        case 461:
            return "Unsupported Transport";
        case 500:
            return "Internal Server Status";
        default:
            return "Status";
    }
}

int Base64Value(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

}  // namespace

std::string Trim(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string Lower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool ContainsNoCase(const std::string& value, const std::string& needle) {
    return Lower(value).find(Lower(needle)) != std::string::npos;
}

std::string HeaderValue(const RtspRequest& request, const std::string& name) {
    const auto it = request.headers.find(Lower(name));
    if (it == request.headers.end()) {
        return std::string();
    }
    return it->second;
}

bool ParseRtspRequest(const std::string& raw, RtspRequest* request) {
    if (request == nullptr) {
        return false;
    }
    std::istringstream input(raw);
    std::string line;
    if (!std::getline(input, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    std::istringstream start(line);
    std::string version;
    if (!(start >> request->method >> request->uri >> version) ||
        version != "RTSP/1.0") {
        return false;
    }

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            return false;
        }
        request->headers[Lower(Trim(line.substr(0, colon)))] =
            Trim(line.substr(colon + 1));
    }
    return true;
}

std::string CSeq(const RtspRequest& request) {
    const std::string cseq = HeaderValue(request, "CSeq");
    return cseq.empty() ? "1" : cseq;
}

std::string BuildRtspResponse(
    int status,
    const std::string& cseq,
    const std::map<std::string, std::string>& headers,
    const std::string& body) {
    std::ostringstream output;
    output << "RTSP/1.0 " << status << " " << RtspStatusText(status) << "\r\n";
    output << "CSeq: " << cseq << "\r\n";
    for (const auto& entry : headers) {
        output << entry.first << ": " << entry.second << "\r\n";
    }
    if (!body.empty()) {
        output << "Content-Length: " << body.size() << "\r\n";
    }
    output << "\r\n";
    output << body;
    return output.str();
}

bool PathToStreamId(const std::string& uri, infra::StreamId* stream_id) {
    const size_t scheme = uri.find("://");
    std::string path = uri;
    if (scheme != std::string::npos) {
        const size_t slash = uri.find('/', scheme + 3);
        path = (slash == std::string::npos) ? std::string() : uri.substr(slash);
    }
    const size_t query = path.find('?');
    if (query != std::string::npos) {
        path = path.substr(0, query);
    }
    if (path == "/live/main") {
        *stream_id = infra::StreamId::kMain;
        return true;
    }
    if (path == "/live/sub") {
        *stream_id = infra::StreamId::kSub;
        return true;
    }
    return false;
}

const char* StreamPath(infra::StreamId stream_id) {
    return stream_id == infra::StreamId::kSub ? "/live/sub" : "/live/main";
}

std::string BuildSdp(const RtspListenAddress& address,
                     infra::StreamId stream_id) {
    std::ostringstream sdp;
    sdp << "v=0\r\n";
    sdp << "o=- 0 0 IN IP4 " << address.ip << "\r\n";
    sdp << "s=live_stream\r\n";
    sdp << "c=IN IP4 0.0.0.0\r\n";
    sdp << "t=0 0\r\n";
    sdp << "a=control:*\r\n";
    sdp << "m=video 0 RTP/AVP " << static_cast<int>(kPayloadTypeH264) << "\r\n";
    sdp << "a=rtpmap:" << static_cast<int>(kPayloadTypeH264)
        << " H264/90000\r\n";
    sdp << "a=control:" << StreamPath(stream_id) << "\r\n";
    return sdp.str();
}

int ParseClientRtpPort(const std::string& transport) {
    const std::string key = "client_port=";
    const size_t pos = Lower(transport).find(key);
    if (pos == std::string::npos) {
        return 0;
    }
    const size_t begin = pos + key.size();
    const size_t end = transport.find_first_of("-;\r\n", begin);
    const std::string value = transport.substr(begin, end - begin);
    return std::atoi(value.c_str());
}

std::string BasicRealmHeader() {
    return "Basic realm=\"live_stream\"";
}

bool DecodeBase64(const std::string& encoded, std::string* decoded) {
    int bits = 0;
    int value = 0;
    decoded->clear();
    for (char ch : encoded) {
        if (ch == '=') {
            break;
        }
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            continue;
        }
        const int digit = Base64Value(ch);
        if (digit < 0) {
            return false;
        }
        value = (value << 6) | digit;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded->push_back(static_cast<char>((value >> bits) & 0xff));
        }
    }
    return true;
}

}  // namespace rtsp_internal
}  // namespace live_stream
