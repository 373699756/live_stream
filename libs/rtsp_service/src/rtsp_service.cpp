#include "rtsp_service.h"

#include "auth_service.h"
#include "event_service.h"
#include "media/frame_source.h"
#include "media_service.h"
#include "netframe_service.h"
#include "rtp_packetizer.h"
#include "rtsp_protocol.h"

#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char* kServiceName = "rtsp_service";
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

}  // namespace

using rtsp_internal::BasicRealmHeader;
using rtsp_internal::AppendU16;
using rtsp_internal::BuildRtspResponse;
using rtsp_internal::BuildSdp;
using rtsp_internal::CSeq;
using rtsp_internal::ContainsNoCase;
using rtsp_internal::DecodeBase64;
using rtsp_internal::HeaderValue;
using rtsp_internal::ParseClientRtpPort;
using rtsp_internal::ParseRtspRequest;
using rtsp_internal::PathToStreamId;
using rtsp_internal::RtpPacket;
using rtsp_internal::RtpPacketizer;
using rtsp_internal::RtspRequest;
using rtsp_internal::StreamPath;

class RtspServiceImpl : public IRtspService,
                        public IRtspFrameSink,
                        public IFrameSink {
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
        } else if (dependencies_.media_service != nullptr) {
            FrameSubscribeOptions main_options;
            main_options.stream_id = infra::StreamId::kMain;
            main_options.require_key_frame_first = true;
            main_options.sink_name = kServiceName;
            auto main_sub =
                dependencies_.media_service->SubscribeFrames(main_options, this);
            if (main_sub.IsOk()) {
                main_subscription_id_ = main_sub.value;
            }
            FrameSubscribeOptions sub_options;
            sub_options.stream_id = infra::StreamId::kSub;
            sub_options.require_key_frame_first = true;
            sub_options.sink_name = kServiceName;
            auto sub_sub =
                dependencies_.media_service->SubscribeFrames(sub_options, this);
            if (sub_sub.IsOk()) {
                sub_subscription_id_ = sub_sub.value;
            }
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
        } else if (dependencies_.media_service != nullptr) {
            if (main_subscription_id_ != 0) {
                (void)dependencies_.media_service->UnsubscribeFrames(
                    main_subscription_id_);
                main_subscription_id_ = 0;
            }
            if (sub_subscription_id_ != 0) {
                (void)dependencies_.media_service->UnsubscribeFrames(
                    sub_subscription_id_);
                sub_subscription_id_ = 0;
            }
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
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != ServiceState::kStarted) {
            return infra::Result<RtspListenAddress>::Fail(infra::Status::kBusy);
        }
        return infra::Result<RtspListenAddress>::Ok(local_address_);
    }

    RtspServiceStats GetStats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
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
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& entry : sessions_) {
                const auto& session = entry.second;
                if (session->state == SessionState::kPlaying &&
                    session->stream_id == frame.stream_id) {
                    targets.push_back(session);
                }
            }
        }
        for (const auto& session : targets) {
            SendFrame(session, frame);
        }
        return infra::Status::kOk;
    }

    void OnFrame(const infra::EncodedFrame& frame) override {
        (void)OnEncodedFrame(frame);
    }

    void OnSourceStateChanged(infra::StreamId stream_id,
                              StreamState state) override {
        (void)stream_id;
        (void)state;
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
        auto session = std::make_shared<Session>();
        session->connection_id = connection_id;
        session->peer = std::move(peer);
        bool accepted = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (sessions_.size() < options_.max_sessions) {
                session->session_id = next_session_id_++;
                session->ssrc =
                    kDefaultSsrcBase ^ static_cast<uint32_t>(session->session_id);
                session->stats.session_id = session->session_id;
                sessions_[connection_id] = session;
                ++stats_.total_sessions;
                accepted = true;
            }
        }
        if (!accepted) {
            (void)dependencies_.net_engine->Close(connection_id);
            return;
        }
        PublishEvent(EventType::kRtspClientConnected, session->peer.ip);
    }

    void OnConnectionClosed(ConnectionId id) {
        std::shared_ptr<Session> session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = sessions_.find(id);
            if (it == sessions_.end()) {
                return;
            }
            session = it->second;
            sessions_.erase(it);
        }
        PublishEvent(EventType::kRtspClientDisconnected, session->peer.ip);
    }

    void OnMessage(ConnectionId connection_id,
                   const uint8_t* data,
                   uint32_t size) {
        std::shared_ptr<Session> session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = sessions_.find(connection_id);
            if (it == sessions_.end()) {
                (void)dependencies_.net_engine->Close(connection_id);
                return;
            }
            session = it->second;
            session->request_buffer.append(reinterpret_cast<const char*>(data), size);
        }
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
                    AddParseFailure();
                    (void)dependencies_.net_engine->Close(connection_id);
                }
                return;
            }
            const std::string raw = session->request_buffer.substr(0, end + 4);
            session->request_buffer.erase(0, end + 4);
            RtspRequest request;
            if (!ParseRtspRequest(raw, &request)) {
                AddParseFailure();
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
            if (RequestKeyFrame(session->stream_id)) {
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
            AddAuthFailure();
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        std::string decoded;
        if (!DecodeBase64(authorization.substr(prefix.size()), &decoded)) {
            AddAuthFailure();
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        const size_t colon = decoded.find(':');
        if (colon == std::string::npos) {
            AddAuthFailure();
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
            AddAuthFailure();
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        const std::string target = StreamPath(stream_id);
        if (dependencies_.auth_service->CheckPermission(
                login_result.value.principal, AuthPermission::kPreviewVideo,
                target) != infra::Status::kOk) {
            AddAuthFailure();
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
            AddTcpInterleavedSession();
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
            AddUdpSession();
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
        bool should_drop = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!session->keyframe_seen) {
                if (frame.frame_type != infra::FrameType::kIdr &&
                    frame.frame_type != infra::FrameType::kI) {
                    ++session->stats.dropped_frames;
                    ++stats_.dropped_frames;
                    should_drop = true;
                } else {
                    session->keyframe_seen = true;
                }
            }
        }
        if (should_drop) {
            NotifyAdaptive(*session, RtspAdaptiveEventType::kFrameDropped);
            return;
        }
        uint16_t sequence = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sequence = session->rtp_sequence;
        }
        RtpPacketizer packetizer(options_.rtp_mtu_bytes);
        std::vector<RtpPacket> packets =
            packetizer.Packetize(frame, &sequence, session->ssrc);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session->rtp_sequence = sequence;
        }
        for (const RtpPacket& packet : packets) {
            SendRtpPacketBytes(session, packet.bytes);
        }
    }

    void SendRtpPacketBytes(const std::shared_ptr<Session>& session,
                            const std::vector<uint8_t>& packet) {
        RtspTransportMode transport = RtspTransportMode::kTcpInterleaved;
        ConnectionId connection_id = 0;
        UdpSocketId udp_socket_id = 0;
        NetAddress target;
        uint8_t interleaved_channel = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            transport = session->transport;
            connection_id = session->connection_id;
            udp_socket_id = udp_socket_id_;
            target = session->peer;
            target.port = session->client_rtp_port;
            interleaved_channel = session->interleaved_rtp_channel;
        }

        infra::Status error = infra::Status::kOk;
        if (transport == RtspTransportMode::kTcpInterleaved) {
            std::vector<uint8_t> framed;
            framed.reserve(packet.size() + 4);
            framed.push_back('$');
            framed.push_back(interleaved_channel);
            AppendU16(&framed, static_cast<uint16_t>(packet.size()));
            framed.insert(framed.end(), packet.begin(), packet.end());
            error = dependencies_.net_engine->Send(connection_id, framed.data(),
                                                   framed.size());
        } else if (udp_socket_id != 0) {
            error = dependencies_.net_engine->SendTo(udp_socket_id, target,
                                                     packet.data(),
                                                     packet.size());
        }
        if (error != infra::Status::kOk) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++session->stats.dropped_frames;
                ++stats_.dropped_frames;
            }
            NotifyAdaptive(*session, RtspAdaptiveEventType::kFrameDropped);
            if (error == infra::Status::kBusy) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++stats_.slow_client_closes;
                }
                NotifyAdaptive(*session, RtspAdaptiveEventType::kSlowClientClosed);
                (void)dependencies_.net_engine->Close(connection_id);
            }
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++session->stats.sent_rtp_packets;
            session->stats.sent_rtp_bytes += packet.size();
            session->stats.pending_bytes =
                dependencies_.net_engine->PendingBytes(connection_id);
            ++stats_.sent_rtp_packets;
            stats_.sent_rtp_bytes += packet.size();
        }
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

    void AddParseFailure() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.parse_failures;
    }

    void AddAuthFailure() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.auth_failures;
    }

    void AddTcpInterleavedSession() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.tcp_interleaved_sessions;
    }

    void AddUdpSession() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.udp_sessions;
    }

    bool RequestKeyFrame(infra::StreamId stream_id) {
        if (dependencies_.frame_source != nullptr) {
            return dependencies_.frame_source->RequestKeyFrame(stream_id) ==
                infra::Status::kOk;
        }
        if (dependencies_.media_service != nullptr) {
            return dependencies_.media_service->RequestKeyFrame(
                       stream_id, KeyFrameReason::kNewClient) ==
                infra::Status::kOk;
        }
        return false;
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
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sample.session = session.stats;
        }
        (void)dependencies_.adaptive_observer->OnRtspAdaptiveSample(sample);
    }

    RtspServiceOptions options_;
    RtspServiceDependencies dependencies_;
    mutable std::mutex mutex_;
    ServiceState state_ = ServiceState::kCreated;
    TcpServerId server_id_ = 0;
    UdpSocketId udp_socket_id_ = 0;
    FrameSubscriptionId main_subscription_id_ = 0;
    FrameSubscriptionId sub_subscription_id_ = 0;
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
