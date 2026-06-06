#include "rtsp_service.h"

#include "auth_service.h"
#include "event_service.h"
#include "infra/log.h"
#include "infra/time.h"
#include "byte_writer.h"
#include "media/media_buffer.h"
#include "net_service.h"
#include "rtsp_protocol.h"
#include "rtsp_session_store.h"
#include "stream_mux.h"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char* kServiceName = "rtsp_service";
constexpr int64_t kRtspPeerAuthTtlMs = 10000;
enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

void RefVideoBufferOwner(const void *owner) {
    (void)VideoBufferRef(
        const_cast<VideoBuffer*>(static_cast<const VideoBuffer*>(owner)));
}

void UnrefVideoBufferOwner(const void *owner) {
    VideoBufferUnref(
        const_cast<VideoBuffer*>(static_cast<const VideoBuffer*>(owner)));
}

NetBufferOwner VideoBufferNetOwner(VideoBuffer *buffer) {
    if (buffer == nullptr) {
        return NetBufferOwner{};
    }
    return NetBufferOwner{buffer, RefVideoBufferOwner,
                          UnrefVideoBufferOwner};
}

struct RtspPeerAuthGrant {
    std::string peer_ip;
    StreamId stream_id = StreamId::kMain;
    std::string user_name;
    int64_t expires_at_ms = 0;
};

}  // namespace

using rtsp_internal::BasicRealmHeader;
using rtsp_internal::BuildRtspResponse;
using rtsp_internal::BuildSdp;
using rtsp_internal::ContainsNoCase;
using rtsp_internal::CSeq;
using rtsp_internal::DecodeBase64;
using rtsp_internal::HeaderValue;
using rtsp_internal::ParseClientRtpPort;
using rtsp_internal::ParseRtspRequest;
using rtsp_internal::PathToStreamId;
using rtsp_internal::RtspRequest;
using rtsp_internal::StreamPath;
using byte_writer::WriteU16;
using stream_mux::IRtpPacketSink;
using stream_mux::RtpPacketizer;
using stream_mux::RtpPacketView;

class RtspServiceImpl : public IRtspService, public IFrameSink {
public:
    RtspServiceImpl(RtspServiceOptions options,
                    RtspServiceDependencies dependencies)
        : options_(std::move(options)),
          dependencies_(dependencies),
          packetizer_(options_.rtp_mtu_bytes) {}

    ~RtspServiceImpl() override {
        ReleaseInternal();
    }

    bool Prepare() {
        if (state_ == ServiceState::kInitialized ||
            state_ == ServiceState::kStarted ||
            state_ == ServiceState::kStopped) {
            return true;
        }
        if (dependencies_.net_engine == nullptr ||
            dependencies_.media_source == nullptr ||
            options_.max_sessions == 0 || options_.rtp_mtu_bytes < 64 ||
            options_.max_request_bytes == 0) {
            return false;
        }
        state_ = ServiceState::kInitialized;
        return true;
    }

    bool Start() override {
        if (state_ == ServiceState::kStarted) {
            return true;
        }
        if (state_ == ServiceState::kCreated ||
            state_ == ServiceState::kDeinitialized) {
            if (!Prepare()) {
                return false;
            }
        }
        if (state_ != ServiceState::kInitialized &&
            state_ != ServiceState::kStopped) {
            return false;
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
        TcpServerId server_result = dependencies_.net_engine->ListenTcp(
            tcp_config, tcp_callbacks);
        if (server_result == 0) {
            return false;
        }
        server_id_ = server_result;

        UdpBindOptions udp_config;
        udp_config.address = {options_.listen_ip, 0};
        UdpCallbacks udp_callbacks;
        udp_callbacks.user = this;
        UdpSocketId udp_result = dependencies_.net_engine->BindUdp(
            udp_config, udp_callbacks);
        if (udp_result != 0) {
            udp_socket_id_ = udp_result;
        }
        FrameAttachOptions main_options;
        main_options.stream_id = StreamId::kMain;
        main_options.require_key_frame_first = true;
        main_options.sink_name = kServiceName;
        main_sink_id_ =
            dependencies_.media_source->AttachFrameSink(main_options, this);
        FrameAttachOptions sub_options;
        sub_options.stream_id = StreamId::kSub;
        sub_options.require_key_frame_first = true;
        sub_options.sink_name = kServiceName;
        sub_sink_id_ =
            dependencies_.media_source->AttachFrameSink(sub_options, this);
        NetAddress local_result =
            dependencies_.net_engine->TcpLocalAddress(server_id_);
        local_address_ = {options_.listen_ip,
                          local_result.port != 0 ? local_result.port
                                                 : options_.listen_port};
        state_ = ServiceState::kStarted;
        return true;
    }

    void Stop() override {
        StopInternal();
    }

    void Release() {
        ReleaseInternal();
    }

private:
    void StopInternal() {
        if (dependencies_.media_source != nullptr && main_sink_id_ != 0) {
            (void)dependencies_.media_source->DetachFrameSink(main_sink_id_);
            main_sink_id_ = 0;
        }
        if (dependencies_.media_source != nullptr && sub_sink_id_ != 0) {
            (void)dependencies_.media_source->DetachFrameSink(sub_sink_id_);
            sub_sink_id_ = 0;
        }
        if (server_id_ != 0 && dependencies_.net_engine != nullptr) {
            (void)dependencies_.net_engine->CloseTcp(server_id_);
            server_id_ = 0;
        }
        if (udp_socket_id_ != 0 && dependencies_.net_engine != nullptr) {
            (void)dependencies_.net_engine->CloseUdp(udp_socket_id_);
            udp_socket_id_ = 0;
        }
        sessions_.Clear();
        peer_auth_grants_.clear();
        if (state_ == ServiceState::kStarted ||
            state_ == ServiceState::kInitialized) {
            state_ = ServiceState::kStopped;
        }
    }

    void ReleaseInternal() {
        StopInternal();
        if (state_ != ServiceState::kCreated) {
            state_ = ServiceState::kDeinitialized;
        }
    }

public:
    const char* Name() const override {
        return kServiceName;
    }

    RtspListenAddress LocalAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != ServiceState::kStarted) {
            return RtspListenAddress{};
        }
        return local_address_;
    }

    RtspServiceStats GetStats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        RtspServiceStats stats = stats_;
        stats.active_sessions = static_cast<uint32_t>(sessions_.Size());
        return stats;
    }

    bool OnEncodedFrame(const EncodedFrame& frame) {
        if (!IsValidFrame(frame)) {
            return false;
        }
        std::vector<std::shared_ptr<RtspSession>> targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            targets = sessions_.PlayingTargets(frame.stream_id);
        }
        for (const auto& session : targets) {
            SendFrame(session, frame);
        }
        return true;
    }

    void OnFrame(const FramePayload& frame) override {
        (void)OnEncodedFrame(frame.encoded_frame);
    }

    void OnSourceStateChanged(StreamId stream_id,
                              StreamState state) override {
        (void)stream_id;
        (void)state;
    }

private:
    bool IsValidFrame(const EncodedFrame& frame) const {
        return frame.buffer != nullptr && frame.buffer->data != nullptr &&
               frame.size > 0 &&
               frame.offset <= frame.buffer->size &&
               frame.size <= frame.buffer->size - frame.offset;
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
        std::shared_ptr<RtspSession> session;
        bool accepted = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (sessions_.Add(connection_id, std::move(peer),
                              options_.max_sessions, &session)) {
                ++stats_.total_sessions;
                accepted = true;
            }
        }
        if (!accepted) {
            (void)dependencies_.net_engine->Close(connection_id);
            return;
        }
        INFRA_LOG_INFO("rtsp_service", "RTSP client connected conn=%llu peer=%s:%u",
                       static_cast<unsigned long long>(connection_id),
                       session->peer.ip.c_str(),
                       static_cast<unsigned>(session->peer.port));
        PublishEvent(EventType::kRtspClientConnected, session->peer.ip);
    }

    void OnConnectionClosed(ConnectionId id) {
        std::shared_ptr<RtspSession> session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = sessions_.Remove(id);
        }
        if (!session) {
            return;
        }
        INFRA_LOG_INFO("rtsp_service",
                       "RTSP client disconnected conn=%llu peer=%s:%u",
                       static_cast<unsigned long long>(id),
                       session->peer.ip.c_str(),
                       static_cast<unsigned>(session->peer.port));
        PublishEvent(EventType::kRtspClientDisconnected, session->peer.ip);
    }

    void OnMessage(ConnectionId connection_id,
                   const uint8_t* data,
                   uint32_t size) {
        std::shared_ptr<RtspSession> session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = sessions_.Find(connection_id);
            if (!session) {
                (void)dependencies_.net_engine->Close(connection_id);
                return;
            }
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

    void HandleRequest(const std::shared_ptr<RtspSession>& session,
                       const RtspRequest& request) {
        INFRA_LOG_INFO("rtsp_service",
                       "RTSP request conn=%llu peer=%s:%u method=%s uri=%s",
                       static_cast<unsigned long long>(session->connection_id),
                       session->peer.ip.c_str(),
                       static_cast<unsigned>(session->peer.port),
                       request.method.c_str(), request.uri.c_str());
        if (request.method == "OPTIONS") {
            SendResponse(session->connection_id, 200, CSeq(request),
                         {{"Public", "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN"}},
                         "");
            return;
        }

        StreamId stream_id = session->stream_id;
        if ((request.method == "DESCRIBE" || request.method == "SETUP") &&
            !PathToStreamId(request.uri, &stream_id)) {
            INFRA_LOG_ERROR("rtsp_service", "RTSP path not found uri=%s",
                            request.uri.c_str());
            SendResponse(session->connection_id, 404, CSeq(request), {}, "");
            return;
        }
        if ((request.method == "DESCRIBE" || request.method == "SETUP") &&
            !IsStreamAvailable(stream_id)) {
            INFRA_LOG_ERROR("rtsp_service", "RTSP stream unavailable uri=%s",
                            request.uri.c_str());
            SendResponse(session->connection_id, 404, CSeq(request), {}, "");
            return;
        }

        if (!Authorize(session, request, stream_id)) {
            return;
        }

        if (request.method == "DESCRIBE") {
            session->stream_id = stream_id;
            const std::string sdp =
                BuildSdp(local_address_, stream_id, CodecForStream(stream_id));
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
            if (session->state != RtspSessionState::kReady &&
                session->state != RtspSessionState::kPlaying) {
                SendResponse(session->connection_id, 455, CSeq(request), {}, "");
                return;
            }
            session->state = RtspSessionState::kPlaying;
            session->keyframe_seen = false;
            session->stats.stream_id = session->stream_id;
            session->stats.transport = session->transport;
            if (RequestKeyFrame(session->stream_id)) {
                NotifyAdaptive(*session, RtspAdaptiveEventType::kKeyFrameRequested);
            }
            INFRA_LOG_INFO("rtsp_service",
                           "RTSP play conn=%llu stream=%s transport=%s",
                           static_cast<unsigned long long>(
                               session->connection_id),
                           StreamPath(session->stream_id),
                           session->transport ==
                                   RtspTransportMode::kTcpInterleaved
                               ? "tcp"
                               : "udp");
            SendResponse(session->connection_id, 200, CSeq(request),
                         {{"Session", std::to_string(session->session_id)},
                          {"RTP-Info", "url=" + std::string(StreamPath(session->stream_id))}},
                         "");
            return;
        }
        if (request.method == "TEARDOWN") {
            SendResponse(session->connection_id, 200, CSeq(request),
                         {{"Session", std::to_string(session->session_id)}}, "");
            session->state = RtspSessionState::kClosed;
            (void)dependencies_.net_engine->CloseAfterSend(session->connection_id);
            return;
        }

        SendResponse(session->connection_id, 455, CSeq(request), {}, "");
    }

    bool IsStreamAvailable(StreamId stream_id) const {
        return dependencies_.media_source != nullptr &&
               dependencies_.media_source->IsStreamAvailable(stream_id);
    }

    VideoCodec CodecForStream(StreamId stream_id) const {
        if (dependencies_.media_source != nullptr &&
            dependencies_.media_source->IsStreamAvailable(stream_id)) {
            return dependencies_.media_source->GetStreamCodec(stream_id);
        }
        if (stream_id == StreamId::kSub) {
            return options_.sub_stream_codec;
        }
        return options_.main_stream_codec;
    }

    bool Authorize(const std::shared_ptr<RtspSession>& session,
                   const RtspRequest& request,
                   StreamId stream_id) {
        if (!options_.enable_auth) {
            return true;
        }
        if (dependencies_.auth_service == nullptr) {
            INFRA_LOG_ERROR("rtsp_service", "RTSP auth service unavailable");
            SendResponse(session->connection_id, 500, CSeq(request), {}, "");
            return false;
        }
        if (session->authenticated &&
            session->authenticated_stream_id == stream_id) {
            return true;
        }
        const std::string authorization = HeaderValue(request, "Authorization");
        const std::string prefix = "Basic ";
        if (authorization.compare(0, prefix.size(), prefix) != 0) {
            std::string cached_user_name;
            if (FindPeerAuthGrant(session->peer.ip, stream_id,
                                  &cached_user_name)) {
                session->authenticated = true;
                session->authenticated_stream_id = stream_id;
                session->authenticated_user = cached_user_name;
                INFRA_LOG_INFO("rtsp_service",
                               "RTSP auth reused peer=%s user=%s uri=%s",
                               session->peer.ip.c_str(),
                               cached_user_name.c_str(), request.uri.c_str());
                return true;
            }
            AddAuthFailure();
            INFRA_LOG_INFO("rtsp_service",
                           "RTSP auth required peer=%s uri=%s",
                           session->peer.ip.c_str(), request.uri.c_str());
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        std::string decoded;
        if (!DecodeBase64(authorization.substr(prefix.size()), &decoded)) {
            AddAuthFailure();
            INFRA_LOG_ERROR("rtsp_service",
                            "RTSP auth invalid base64 peer=%s",
                            session->peer.ip.c_str());
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        const size_t colon = decoded.find(':');
        if (colon == std::string::npos) {
            AddAuthFailure();
            INFRA_LOG_ERROR("rtsp_service",
                            "RTSP auth invalid credential peer=%s",
                            session->peer.ip.c_str());
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        LoginRequest login;
        login.context.client_ip = session->peer.ip;
        login.user_name = decoded.substr(0, colon);
        login.password = decoded.substr(colon + 1);
        LoginResult login_result = dependencies_.auth_service->Login(login);
        if (login_result.token.empty()) {
            AddAuthFailure();
            INFRA_LOG_ERROR("rtsp_service",
                            "RTSP auth rejected peer=%s user=%s",
                            session->peer.ip.c_str(), login.user_name.c_str());
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        live_stream::RequestContext logout_context;
        logout_context.user_name = login_result.principal.user_name;
        logout_context.session_id = login_result.principal.session_id;
        if (login_result.must_change_password) {
            static_cast<void>(
                dependencies_.auth_service->Logout(logout_context));
            AddAuthFailure();
            INFRA_LOG_ERROR("rtsp_service",
                            "RTSP auth rejected peer=%s user=%s "
                            "reason=must_change_password",
                            session->peer.ip.c_str(), login.user_name.c_str());
            SendResponse(session->connection_id, 403, CSeq(request), {}, "");
            return false;
        }
        const std::string target = StreamPath(stream_id);
        if (!dependencies_.auth_service->CheckPermission(
                login_result.principal, AuthPermission::kPreviewVideo,
                target)) {
            static_cast<void>(
                dependencies_.auth_service->Logout(logout_context));
            AddAuthFailure();
            INFRA_LOG_ERROR("rtsp_service",
                            "RTSP auth forbidden peer=%s user=%s target=%s",
                            session->peer.ip.c_str(), login.user_name.c_str(),
                            target.c_str());
            SendResponse(session->connection_id, 403, CSeq(request), {}, "");
            return false;
        }
        static_cast<void>(dependencies_.auth_service->Logout(logout_context));
        session->authenticated = true;
        session->authenticated_stream_id = stream_id;
        session->authenticated_user = login_result.principal.user_name;
        RememberPeerAuthGrant(session->peer.ip, stream_id,
                              login_result.principal.user_name);
        INFRA_LOG_INFO("rtsp_service",
                       "RTSP auth accepted peer=%s user=%s target=%s",
                       session->peer.ip.c_str(),
                       login_result.principal.user_name.c_str(),
                       target.c_str());
        return true;
    }

    void HandleSetup(const std::shared_ptr<RtspSession>& session,
                     const RtspRequest& request,
                     StreamId stream_id) {
        const std::string transport = HeaderValue(request, "Transport");
        if (transport.empty()) {
            INFRA_LOG_ERROR("rtsp_service", "RTSP setup missing Transport");
            SendResponse(session->connection_id, 461, CSeq(request), {}, "");
            return;
        }
        session->stream_id = stream_id;
        session->state = RtspSessionState::kReady;
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
                INFRA_LOG_ERROR(
                    "rtsp_service",
                    "RTSP setup unsupported UDP transport=%s udp=%llu",
                    transport.c_str(),
                    static_cast<unsigned long long>(udp_socket_id_));
                SendResponse(session->connection_id, 461, CSeq(request), {}, "");
                return;
            }
            session->transport = RtspTransportMode::kUdp;
            session->client_rtp_port = static_cast<uint16_t>(client_port);
            const NetAddress server_rtp =
                dependencies_.net_engine->UdpLocalAddress(udp_socket_id_);
            response_transport = "RTP/AVP;unicast;client_port=" +
                                 std::to_string(client_port) + "-" +
                                 std::to_string(client_port + 1) +
                                 ";server_port=" +
                                 std::to_string(server_rtp.port) + "-" +
                                 std::to_string(server_rtp.port + 1);
            AddUdpSession();
        } else {
            INFRA_LOG_ERROR("rtsp_service",
                            "RTSP setup unsupported transport=%s",
                            transport.c_str());
            SendResponse(session->connection_id, 461, CSeq(request), {}, "");
            return;
        }
        session->stats.transport = session->transport;
        session->stats.stream_id = session->stream_id;
        RememberPeerAuthGrant(session->peer.ip, stream_id,
                              session->authenticated_user);
        INFRA_LOG_INFO("rtsp_service",
                       "RTSP setup conn=%llu stream=%s transport=%s",
                       static_cast<unsigned long long>(
                           session->connection_id),
                       StreamPath(stream_id),
                       session->transport == RtspTransportMode::kTcpInterleaved
                           ? "tcp"
                           : "udp");
        SendResponse(session->connection_id, 200, CSeq(request),
                     {{"Transport", response_transport},
                      {"Session", std::to_string(session->session_id)}},
                     "");
    }

    class RtspPacketSink final : public IRtpPacketSink {
    public:
        RtspPacketSink(RtspServiceImpl* service,
                       std::shared_ptr<RtspSession> session,
                       const EncodedFrame* frame)
            : service_(service),
              session_(std::move(session)),
              frame_(frame) {}

        bool OnRtpPacket(const RtpPacketView& packet) override {
            if (service_ == nullptr || frame_ == nullptr || !ok_) {
                return false;
            }
            ok_ = service_->SendRtpPacketView(session_, *frame_, packet);
            return ok_;
        }

        bool ok() const { return ok_; }

    private:
        RtspServiceImpl* service_ = nullptr;
        std::shared_ptr<RtspSession> session_;
        const EncodedFrame* frame_ = nullptr;
        bool ok_ = true;
    };

    void SendFrame(const std::shared_ptr<RtspSession>& session,
                   const EncodedFrame& frame) {
        bool should_drop = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!session->keyframe_seen) {
                if (frame.frame_type != FrameType::kIdr &&
                    frame.frame_type != FrameType::kI) {
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
        RtspPacketSink sink(this, session, &frame);
        const bool packetized =
            packetizer_.Packetize(frame, &sequence, session->ssrc, &sink);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session->rtp_sequence = sequence;
        }
        if (!packetized) {
            NotifyAdaptive(*session, RtspAdaptiveEventType::kFrameDropped);
        }
    }

    bool SendRtpPacketView(const std::shared_ptr<RtspSession>& session,
                           const EncodedFrame& frame,
                           const RtpPacketView& packet) {
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

        const size_t packet_size = packet.Size();
        if (packet_size == 0 || packet_size > 0xffff ||
            dependencies_.net_engine == nullptr) {
            return false;
        }
        const NetBufferOwner payload_owner = VideoBufferNetOwner(frame.buffer);
        NetBufferSlices slices;
        uint8_t interleaved_header[4] = {'$', interleaved_channel, 0, 0};
        bool ok = true;
        if (transport == RtspTransportMode::kTcpInterleaved) {
            WriteU16(interleaved_header + 2,
                     static_cast<uint16_t>(packet_size));
            ok = slices.Add(interleaved_header, sizeof(interleaved_header));
            for (size_t i = 0; ok && i < packet.slice_count; ++i) {
                const auto& slice = packet.slices[i];
                ok = slices.Add(slice.data, slice.size,
                                slice.media_payload ? payload_owner
                                                    : NetBufferOwner{});
            }
            ok = ok && dependencies_.net_engine->SendSlices(connection_id,
                                                            slices);
        } else if (udp_socket_id != 0) {
            for (size_t i = 0; ok && i < packet.slice_count; ++i) {
                const auto& slice = packet.slices[i];
                ok = slices.Add(slice.data, slice.size,
                                slice.media_payload ? payload_owner
                                                    : NetBufferOwner{});
            }
            ok = ok && dependencies_.net_engine->SendToSlices(udp_socket_id,
                                                              target, slices);
        }
        if (!ok) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++session->stats.dropped_frames;
                ++stats_.dropped_frames;
            }
            NotifyAdaptive(*session, RtspAdaptiveEventType::kFrameDropped);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++stats_.slow_client_closes;
            }
            NotifyAdaptive(*session, RtspAdaptiveEventType::kSlowClientClosed);
            (void)dependencies_.net_engine->Close(connection_id);
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++session->stats.sent_rtp_packets;
            session->stats.sent_rtp_bytes += packet_size;
            session->stats.pending_bytes =
                dependencies_.net_engine->PendingBytes(connection_id);
            ++stats_.sent_rtp_packets;
            stats_.sent_rtp_bytes += packet_size;
        }
        NotifyAdaptive(*session, RtspAdaptiveEventType::kSample);
        return true;
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

    bool FindPeerAuthGrant(const std::string& peer_ip,
                           StreamId stream_id,
                           std::string* user_name) {
        const int64_t now_ms = infra::Time::MonotonicMillis();
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = peer_auth_grants_.begin();
             it != peer_auth_grants_.end();) {
            if (it->expires_at_ms <= now_ms) {
                it = peer_auth_grants_.erase(it);
                continue;
            }
            if (it->peer_ip == peer_ip && it->stream_id == stream_id) {
                if (user_name != nullptr) {
                    *user_name = it->user_name;
                }
                return true;
            }
            ++it;
        }
        return false;
    }

    void RememberPeerAuthGrant(const std::string& peer_ip,
                               StreamId stream_id,
                               const std::string& user_name) {
        if (peer_ip.empty() || user_name.empty()) {
            return;
        }
        const int64_t expires_at_ms =
            infra::Time::MonotonicMillis() + kRtspPeerAuthTtlMs;
        std::lock_guard<std::mutex> lock(mutex_);
        for (RtspPeerAuthGrant& grant : peer_auth_grants_) {
            if (grant.peer_ip == peer_ip && grant.stream_id == stream_id) {
                grant.user_name = user_name;
                grant.expires_at_ms = expires_at_ms;
                return;
            }
        }
        peer_auth_grants_.push_back(
            RtspPeerAuthGrant{peer_ip, stream_id, user_name, expires_at_ms});
    }

    bool RequestKeyFrame(StreamId stream_id) {
        if (dependencies_.media_source != nullptr) {
            return dependencies_.media_source->RequestKeyFrame(
                stream_id, KeyFrameReason::kNewClient);
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

    void NotifyAdaptive(const RtspSession& session, RtspAdaptiveEventType event) {
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
    RtpPacketizer packetizer_;
    mutable std::mutex mutex_;
    ServiceState state_ = ServiceState::kCreated;
    TcpServerId server_id_ = 0;
    UdpSocketId udp_socket_id_ = 0;
    RtspListenAddress local_address_;
    RtspSessionStore sessions_;
    std::vector<RtspPeerAuthGrant> peer_auth_grants_;
    RtspServiceStats stats_;
    FrameAttachId main_sink_id_ = 0;
    FrameAttachId sub_sink_id_ = 0;
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
