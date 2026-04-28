#include "rtsp_service.h"

#include "auth_service.h"
#include "event_service.h"
#include "netframe_service.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char* kServiceName = "rtsp_service";
constexpr uint8_t kPayloadTypeH264 = 96;
constexpr uint8_t kPayloadTypeH265 = 98;
constexpr uint32_t kRtpClockRate = 90000;
constexpr uint32_t kDefaultSsrcBase = 0x52545350;

enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

enum class SessionState {
    kInit = 0,
    kReady,
    kPlaying,
    kClosed,
};

struct RtspRequest {
    std::string method;
    std::string uri;
    std::map<std::string, std::string> headers;
};

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

std::string BuildRtspResponse(int status,
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

uint8_t PayloadType(infra::VideoCodec codec) {
    return codec == infra::VideoCodec::kH265 ? kPayloadTypeH265 : kPayloadTypeH264;
}

uint32_t RtpTimestamp(const infra::EncodedFrame& frame) {
    return static_cast<uint32_t>((frame.pts_us * kRtpClockRate) / 1000000);
}

void AppendU16(std::vector<uint8_t>* out, uint16_t value) {
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out->push_back(static_cast<uint8_t>(value & 0xff));
}

void AppendU32(std::vector<uint8_t>* out, uint32_t value) {
    out->push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out->push_back(static_cast<uint8_t>(value & 0xff));
}

std::string BuildSdp(const RtspListenAddress& address, infra::StreamId stream_id) {
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

}  // namespace

class RtspServiceImpl : public IRtspService, public IRtspFrameSink {
 public:
    RtspServiceImpl(RtspServiceOptions options,
                    RtspServiceDependencies dependencies)
        : options_(std::move(options)), dependencies_(dependencies) {}

    ~RtspServiceImpl() override {
        Deinit();
    }

    infra::Status Init() override {
        if (state_ == ServiceState::kInitialized ||
            state_ == ServiceState::kStarted ||
            state_ == ServiceState::kStopped) {
            return infra::Status::kOk;
        }
        if (dependencies_.net_engine == nullptr ||
            options_.max_sessions == 0 || options_.rtp_mtu_bytes < 64 ||
            options_.max_request_bytes == 0) {
            return infra::Status::kInvalidParam;
        }
        state_ = ServiceState::kInitialized;
        return infra::Status::kOk;
    }

    infra::Status Start() override {
        if (state_ == ServiceState::kStarted) {
            return infra::Status::kOk;
        }
        if (state_ == ServiceState::kCreated ||
            state_ == ServiceState::kDeinitialized) {
            const infra::Status error = Init();
            if (error != infra::Status::kOk) {
                return error;
            }
        }
        if (state_ != ServiceState::kInitialized &&
            state_ != ServiceState::kStopped) {
            return infra::Status::kBusy;
        }

        TcpListenOptions tcp_config;
        tcp_config.address = {options_.listen_ip, options_.listen_port};
        tcp_config.max_connections = options_.max_sessions;
        tcp_config.send_queue_capacity = options_.send_queue_capacity;
        tcp_config.send_buffer_limit_bytes = options_.send_buffer_limit_bytes;
        tcp_config.send_stall_timeout_ms = options_.send_stall_timeout_ms;
        tcp_config.tcp_no_delay = true;
        tcp_config.keepalive = true;
        TcpCallbacks tcp_callbacks;
        tcp_callbacks.user = this;
        tcp_callbacks.on_accept = &RtspServiceImpl::HandleAccept;
        tcp_callbacks.on_read = &RtspServiceImpl::HandleRead;
        tcp_callbacks.on_close = &RtspServiceImpl::HandleClose;
        auto server_result = dependencies_.net_engine->ListenTcp(
            tcp_config, tcp_callbacks);
        if (!server_result.IsOk()) {
            return server_result.status;
        }
        server_id_ = server_result.value;

        UdpBindOptions udp_config;
        udp_config.address = {options_.listen_ip, 0};
        UdpCallbacks udp_callbacks;
        udp_callbacks.user = this;
        auto udp_result = dependencies_.net_engine->BindUdp(
            udp_config, udp_callbacks);
        if (udp_result.IsOk()) {
            udp_socket_id_ = udp_result.value;
        }
        infra::Status error = dependencies_.net_engine->Start();
        if (error != infra::Status::kOk) {
            Stop();
            return error;
        }

        if (dependencies_.frame_source != nullptr) {
            (void)dependencies_.frame_source->AttachSink(infra::StreamId::kMain,
                                                         this);
            (void)dependencies_.frame_source->AttachSink(infra::StreamId::kSub,
                                                         this);
        }
        auto local_result = dependencies_.net_engine->TcpLocalAddress(server_id_);
        local_address_ = {options_.listen_ip,
                          local_result.IsOk() ? local_result.value.port : options_.listen_port};
        state_ = ServiceState::kStarted;
        return infra::Status::kOk;
    }

    void Stop() override {
        if (dependencies_.frame_source != nullptr) {
            (void)dependencies_.frame_source->DetachSink(infra::StreamId::kMain,
                                                         this);
            (void)dependencies_.frame_source->DetachSink(infra::StreamId::kSub,
                                                         this);
        }
        if (server_id_ != 0 && dependencies_.net_engine != nullptr) {
            (void)dependencies_.net_engine->CloseTcp(server_id_);
            server_id_ = 0;
        }
        if (udp_socket_id_ != 0 && dependencies_.net_engine != nullptr) {
            (void)dependencies_.net_engine->CloseUdp(udp_socket_id_);
            udp_socket_id_ = 0;
        }
        sessions_.clear();
        if (state_ == ServiceState::kStarted ||
            state_ == ServiceState::kInitialized) {
            state_ = ServiceState::kStopped;
        }
    }

    void Deinit() override {
        Stop();
        if (state_ != ServiceState::kCreated) {
            state_ = ServiceState::kDeinitialized;
        }
    }

    const char* Name() const override {
        return kServiceName;
    }

    infra::Result<RtspListenAddress> LocalAddress() const override {
        if (state_ != ServiceState::kStarted) {
            return infra::Result<RtspListenAddress>::Fail(infra::Status::kBusy);
        }
        return infra::Result<RtspListenAddress>::Ok(local_address_);
    }

    RtspServiceStats GetStats() const override {
        RtspServiceStats stats = stats_;
        stats.active_sessions = static_cast<uint32_t>(sessions_.size());
        return stats;
    }

    infra::Status PushFrame(const infra::EncodedFrame& frame) override {
        return OnEncodedFrame(frame);
    }

    infra::Status OnEncodedFrame(const infra::EncodedFrame& frame) override {
        if (!IsValidFrame(frame)) {
            return infra::Status::kInvalidParam;
        }
        std::vector<std::shared_ptr<Session>> targets;
        for (const auto& entry : sessions_) {
            const auto& session = entry.second;
            if (session->state == SessionState::kPlaying &&
                session->stream_id == frame.stream_id) {
                targets.push_back(session);
            }
        }
        for (const auto& session : targets) {
            SendFrame(session, frame);
        }
        return infra::Status::kOk;
    }

 private:
    struct Session {
        uint64_t session_id = 0;
        ConnectionId connection_id = 0;
        NetAddress peer;
        SessionState state = SessionState::kInit;
        infra::StreamId stream_id = infra::StreamId::kMain;
        RtspTransportMode transport = RtspTransportMode::kTcpInterleaved;
        uint8_t interleaved_rtp_channel = 0;
        uint16_t client_rtp_port = 0;
        uint16_t rtp_sequence = 1;
        uint32_t ssrc = 0;
        bool keyframe_seen = false;
        std::string request_buffer;
        RtspSessionStats stats;
    };

    bool IsValidFrame(const infra::EncodedFrame& frame) const {
        return frame.buffer && frame.buffer->Data() != nullptr &&
               frame.size > 0 &&
               frame.offset <= frame.buffer->Size() &&
               frame.size <= frame.buffer->Size() - frame.offset;
    }

    static void HandleAccept(void* user, ConnectionId id, NetAddress peer) {
        RtspServiceImpl* self = static_cast<RtspServiceImpl*>(user);
        if (self != nullptr) {
            self->OnConnection(id, std::move(peer));
        }
    }

    static void HandleRead(void* user,
                           ConnectionId id,
                           const uint8_t* data,
                           size_t size) {
        RtspServiceImpl* self = static_cast<RtspServiceImpl*>(user);
        if (self != nullptr) {
            self->OnMessage(id, data, static_cast<uint32_t>(size));
        }
    }

    static void HandleClose(void* user, ConnectionId id) {
        RtspServiceImpl* self = static_cast<RtspServiceImpl*>(user);
        if (self != nullptr) {
            self->OnConnectionClosed(id);
        }
    }

    void OnConnection(ConnectionId connection_id, NetAddress peer) {
        if (sessions_.size() >= options_.max_sessions) {
            (void)dependencies_.net_engine->Close(connection_id);
            return;
        }
        auto session = std::make_shared<Session>();
        session->session_id = next_session_id_++;
        session->connection_id = connection_id;
        session->peer = std::move(peer);
        session->ssrc = kDefaultSsrcBase ^ static_cast<uint32_t>(session->session_id);
        session->stats.session_id = session->session_id;
        sessions_[connection_id] = session;
        ++stats_.total_sessions;
        PublishEvent(EventType::kRtspClientConnected, session->peer.ip);
    }

    void OnConnectionClosed(ConnectionId id) {
        const auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            return;
        }
        PublishEvent(EventType::kRtspClientDisconnected,
                     it->second->peer.ip);
        sessions_.erase(it);
    }

    void OnMessage(ConnectionId connection_id,
                   const uint8_t* data,
                   uint32_t size) {
        const auto it = sessions_.find(connection_id);
        if (it == sessions_.end()) {
            (void)dependencies_.net_engine->Close(connection_id);
            return;
        }
        auto session = it->second;
        session->request_buffer.append(reinterpret_cast<const char*>(data), size);
        while (!session->request_buffer.empty() &&
               session->request_buffer[0] == '$') {
            if (session->request_buffer.size() < 4) {
                return;
            }
            const uint16_t payload_size =
                (static_cast<uint8_t>(session->request_buffer[2]) << 8) |
                static_cast<uint8_t>(session->request_buffer[3]);
            if (session->request_buffer.size() < 4U + payload_size) {
                return;
            }
            session->request_buffer.erase(0, 4U + payload_size);
        }
        while (true) {
            const size_t end = session->request_buffer.find("\r\n\r\n");
            if (end == std::string::npos) {
                if (session->request_buffer.size() > options_.max_request_bytes) {
                    ++stats_.parse_failures;
                    (void)dependencies_.net_engine->Close(connection_id);
                }
                return;
            }
            const std::string raw = session->request_buffer.substr(0, end + 4);
            session->request_buffer.erase(0, end + 4);
            RtspRequest request;
            if (!ParseRtspRequest(raw, &request)) {
                ++stats_.parse_failures;
                SendResponse(connection_id, 400, "1", {}, "");
                (void)dependencies_.net_engine->CloseAfterSend(connection_id);
                return;
            }
            HandleRequest(session, request);
        }
    }

    void HandleRequest(const std::shared_ptr<Session>& session,
                       const RtspRequest& request) {
        if (request.method == "OPTIONS") {
            SendResponse(session->connection_id, 200, CSeq(request),
                         {{"Public", "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN"}},
                         "");
            return;
        }

        infra::StreamId stream_id = infra::StreamId::kMain;
        if ((request.method == "DESCRIBE" || request.method == "SETUP") &&
            !PathToStreamId(request.uri, &stream_id)) {
            SendResponse(session->connection_id, 404, CSeq(request), {}, "");
            return;
        }

        if (!Authorize(session, request, stream_id)) {
            return;
        }

        if (request.method == "DESCRIBE") {
            session->stream_id = stream_id;
            const std::string sdp = BuildSdp(local_address_, stream_id);
            SendResponse(session->connection_id, 200, CSeq(request),
                         {{"Content-Type", "application/sdp"},
                          {"Content-Base", request.uri}},
                         sdp);
            return;
        }
        if (request.method == "SETUP") {
            HandleSetup(session, request, stream_id);
            return;
        }
        if (request.method == "PLAY") {
            if (session->state != SessionState::kReady &&
                session->state != SessionState::kPlaying) {
                SendResponse(session->connection_id, 455, CSeq(request), {}, "");
                return;
            }
            session->state = SessionState::kPlaying;
            session->keyframe_seen = false;
            session->stats.stream_id = session->stream_id;
            session->stats.transport = session->transport;
            if (dependencies_.frame_source != nullptr) {
                (void)dependencies_.frame_source->RequestKeyFrame(
                    session->stream_id);
                NotifyAdaptive(*session, RtspAdaptiveEventType::kKeyFrameRequested);
            }
            SendResponse(session->connection_id, 200, CSeq(request),
                         {{"Session", std::to_string(session->session_id)},
                          {"RTP-Info", "url=" + std::string(StreamPath(session->stream_id))}},
                         "");
            return;
        }
        if (request.method == "TEARDOWN") {
            SendResponse(session->connection_id, 200, CSeq(request),
                         {{"Session", std::to_string(session->session_id)}}, "");
            session->state = SessionState::kClosed;
            (void)dependencies_.net_engine->CloseAfterSend(session->connection_id);
            return;
        }

        SendResponse(session->connection_id, 455, CSeq(request), {}, "");
    }

    bool Authorize(const std::shared_ptr<Session>& session,
                   const RtspRequest& request,
                   infra::StreamId stream_id) {
        if (!options_.enable_auth) {
            return true;
        }
        if (dependencies_.auth_service == nullptr) {
            SendResponse(session->connection_id, 500, CSeq(request), {}, "");
            return false;
        }
        const std::string authorization = HeaderValue(request, "Authorization");
        const std::string prefix = "Basic ";
        if (authorization.compare(0, prefix.size(), prefix) != 0) {
            ++stats_.auth_failures;
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        std::string decoded;
        if (!DecodeBase64(authorization.substr(prefix.size()), &decoded)) {
            ++stats_.auth_failures;
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        const size_t colon = decoded.find(':');
        if (colon == std::string::npos) {
            ++stats_.auth_failures;
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        LoginRequest login;
        login.context.client_ip = session->peer.ip;
        login.user_name = decoded.substr(0, colon);
        login.password = decoded.substr(colon + 1);
        auto login_result = dependencies_.auth_service->Login(login);
        if (!login_result.IsOk()) {
            ++stats_.auth_failures;
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        const std::string target = StreamPath(stream_id);
        if (dependencies_.auth_service->CheckPermission(
                login_result.value.principal, AuthPermission::kPreviewVideo,
                target) != infra::Status::kOk) {
            ++stats_.auth_failures;
            SendResponse(session->connection_id, 403, CSeq(request), {}, "");
            return false;
        }
        return true;
    }

    void HandleSetup(const std::shared_ptr<Session>& session,
                     const RtspRequest& request,
                     infra::StreamId stream_id) {
        const std::string transport = HeaderValue(request, "Transport");
        if (transport.empty()) {
            SendResponse(session->connection_id, 461, CSeq(request), {}, "");
            return;
        }
        session->stream_id = stream_id;
        session->state = SessionState::kReady;
        std::string response_transport;
        if (ContainsNoCase(transport, "RTP/AVP/TCP") ||
            ContainsNoCase(transport, "interleaved")) {
            session->transport = RtspTransportMode::kTcpInterleaved;
            session->interleaved_rtp_channel = 0;
            response_transport =
                "RTP/AVP/TCP;unicast;interleaved=0-1";
            ++stats_.tcp_interleaved_sessions;
        } else if (ContainsNoCase(transport, "RTP/AVP")) {
            const int client_port = ParseClientRtpPort(transport);
            if (client_port <= 0 || client_port > 65535 || udp_socket_id_ == 0) {
                SendResponse(session->connection_id, 461, CSeq(request), {}, "");
                return;
            }
            session->transport = RtspTransportMode::kUdp;
            session->client_rtp_port = static_cast<uint16_t>(client_port);
            response_transport = "RTP/AVP;unicast;client_port=" +
                std::to_string(client_port) + "-" +
                std::to_string(client_port + 1);
            ++stats_.udp_sessions;
        } else {
            SendResponse(session->connection_id, 461, CSeq(request), {}, "");
            return;
        }
        session->stats.transport = session->transport;
        session->stats.stream_id = session->stream_id;
        SendResponse(session->connection_id, 200, CSeq(request),
                     {{"Transport", response_transport},
                      {"Session", std::to_string(session->session_id)}},
                     "");
    }

    void SendFrame(const std::shared_ptr<Session>& session,
                   const infra::EncodedFrame& frame) {
        if (!session->keyframe_seen) {
            if (frame.frame_type != infra::FrameType::kIdr &&
                frame.frame_type != infra::FrameType::kI) {
                ++session->stats.dropped_frames;
                ++stats_.dropped_frames;
                NotifyAdaptive(*session, RtspAdaptiveEventType::kFrameDropped);
                return;
            }
            session->keyframe_seen = true;
        }
        const uint8_t* payload = frame.buffer->Data() + frame.offset;
        uint32_t size = frame.size;
        StripAnnexBStartCode(&payload, &size);
        if (size == 0) {
            return;
        }
        const uint32_t mtu = options_.rtp_mtu_bytes;
        if (size + 12 <= mtu) {
            SendRtpPacket(session, frame, payload, size, true);
            return;
        }
        if (frame.codec == infra::VideoCodec::kH265) {
            PacketizeH265(session, frame, payload, size);
        } else {
            PacketizeH264(session, frame, payload, size);
        }
    }

    void StripAnnexBStartCode(const uint8_t** payload, uint32_t* size) const {
        if (*size >= 4 && (*payload)[0] == 0 && (*payload)[1] == 0 &&
            (*payload)[2] == 0 && (*payload)[3] == 1) {
            *payload += 4;
            *size -= 4;
            return;
        }
        if (*size >= 3 && (*payload)[0] == 0 && (*payload)[1] == 0 &&
            (*payload)[2] == 1) {
            *payload += 3;
            *size -= 3;
        }
    }

    void PacketizeH264(const std::shared_ptr<Session>& session,
                       const infra::EncodedFrame& frame,
                       const uint8_t* payload,
                       uint32_t size) {
        if (size <= 1) {
            return;
        }
        const uint8_t nal = payload[0];
        const uint8_t fu_indicator = (nal & 0xe0) | 28;
        const uint8_t nal_type = nal & 0x1f;
        const uint32_t max_fragment = options_.rtp_mtu_bytes - 14;
        uint32_t offset = 1;
        bool start = true;
        while (offset < size) {
            const uint32_t chunk = std::min(max_fragment, size - offset);
            std::vector<uint8_t> fragment;
            fragment.reserve(chunk + 2);
            fragment.push_back(fu_indicator);
            uint8_t fu_header = nal_type;
            if (start) {
                fu_header |= 0x80;
            }
            if (offset + chunk >= size) {
                fu_header |= 0x40;
            }
            fragment.push_back(fu_header);
            fragment.insert(fragment.end(), payload + offset, payload + offset + chunk);
            SendRtpPacket(session, frame, fragment.data(),
                          static_cast<uint32_t>(fragment.size()),
                          offset + chunk >= size);
            offset += chunk;
            start = false;
        }
    }

    void PacketizeH265(const std::shared_ptr<Session>& session,
                       const infra::EncodedFrame& frame,
                       const uint8_t* payload,
                       uint32_t size) {
        if (size <= 2) {
            return;
        }
        const uint8_t nal0 = payload[0];
        const uint8_t nal1 = payload[1];
        const uint8_t nal_type = (nal0 >> 1) & 0x3f;
        const uint32_t max_fragment = options_.rtp_mtu_bytes - 15;
        uint32_t offset = 2;
        bool start = true;
        while (offset < size) {
            const uint32_t chunk = std::min(max_fragment, size - offset);
            std::vector<uint8_t> fragment;
            fragment.reserve(chunk + 3);
            fragment.push_back((nal0 & 0x81) | (49 << 1));
            fragment.push_back(nal1);
            uint8_t fu_header = nal_type;
            if (start) {
                fu_header |= 0x80;
            }
            if (offset + chunk >= size) {
                fu_header |= 0x40;
            }
            fragment.push_back(fu_header);
            fragment.insert(fragment.end(), payload + offset, payload + offset + chunk);
            SendRtpPacket(session, frame, fragment.data(),
                          static_cast<uint32_t>(fragment.size()),
                          offset + chunk >= size);
            offset += chunk;
            start = false;
        }
    }

    void SendRtpPacket(const std::shared_ptr<Session>& session,
                       const infra::EncodedFrame& frame,
                       const uint8_t* payload,
                       uint32_t size,
                       bool marker) {
        std::vector<uint8_t> packet;
        packet.reserve(size + 16);
        packet.push_back(0x80);
        packet.push_back(static_cast<uint8_t>((marker ? 0x80 : 0x00) |
                                              PayloadType(frame.codec)));
        AppendU16(&packet, session->rtp_sequence++);
        AppendU32(&packet, RtpTimestamp(frame));
        AppendU32(&packet, session->ssrc);
        packet.insert(packet.end(), payload, payload + size);

        infra::Status error = infra::Status::kOk;
        if (session->transport == RtspTransportMode::kTcpInterleaved) {
            std::vector<uint8_t> framed;
            framed.reserve(packet.size() + 4);
            framed.push_back('$');
            framed.push_back(session->interleaved_rtp_channel);
            AppendU16(&framed, static_cast<uint16_t>(packet.size()));
            framed.insert(framed.end(), packet.begin(), packet.end());
            error = dependencies_.net_engine->Send(session->connection_id,
                                                   framed.data(),
                                                   framed.size());
        } else if (udp_socket_id_ != 0) {
            NetAddress target = session->peer;
            target.port = session->client_rtp_port;
            error = dependencies_.net_engine->SendTo(udp_socket_id_, target,
                                                     packet.data(),
                                                     packet.size());
        }
        if (error != infra::Status::kOk) {
            ++session->stats.dropped_frames;
            ++stats_.dropped_frames;
            NotifyAdaptive(*session, RtspAdaptiveEventType::kFrameDropped);
            if (error == infra::Status::kBusy) {
                ++stats_.slow_client_closes;
                NotifyAdaptive(*session, RtspAdaptiveEventType::kSlowClientClosed);
                (void)dependencies_.net_engine->Close(session->connection_id);
            }
            return;
        }
        ++session->stats.sent_rtp_packets;
        session->stats.sent_rtp_bytes += packet.size();
        session->stats.pending_bytes =
            dependencies_.net_engine->PendingBytes(session->connection_id);
        ++stats_.sent_rtp_packets;
        stats_.sent_rtp_bytes += packet.size();
        NotifyAdaptive(*session, RtspAdaptiveEventType::kSample);
    }

    void SendResponse(ConnectionId connection_id,
                      int status,
                      const std::string& cseq,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body) {
        const std::string response = BuildRtspResponse(status, cseq, headers, body);
        (void)dependencies_.net_engine->Send(
            connection_id, reinterpret_cast<const uint8_t*>(response.data()),
            response.size());
    }

    void PublishEvent(EventType type, const std::string& target) {
        if (dependencies_.event_service == nullptr) {
            return;
        }
        Event event;
        event.type = type;
        event.source = kServiceName;
        event.target = target;
        (void)dependencies_.event_service->Publish(event);
    }

    void NotifyAdaptive(const Session& session, RtspAdaptiveEventType event) {
        if (dependencies_.adaptive_observer == nullptr) {
            return;
        }
        RtspAdaptiveSample sample;
        sample.event = event;
        sample.session = session.stats;
        (void)dependencies_.adaptive_observer->OnRtspAdaptiveSample(sample);
    }

    RtspServiceOptions options_;
    RtspServiceDependencies dependencies_;
    ServiceState state_ = ServiceState::kCreated;
    TcpServerId server_id_ = 0;
    UdpSocketId udp_socket_id_ = 0;
    RtspListenAddress local_address_;
    std::map<ConnectionId, std::shared_ptr<Session>> sessions_;
    uint64_t next_session_id_ = 1;
    RtspServiceStats stats_;
};

std::unique_ptr<IRtspService> CreateRtspService(
    const RtspServiceOptions& options,
    const RtspServiceDependencies& dependencies) {
    return std::unique_ptr<IRtspService>(
        new RtspServiceImpl(options, dependencies));
}

const char* RtspService::Name() {
    return kServiceName;
}

}  // namespace live_stream
