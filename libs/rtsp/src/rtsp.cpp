#include "rtsp.h"

#include "auth.h"
#include "event.h"
#include "infra/log.h"
#include "infra/time.h"
#include "media_source.h"
#include "media_mux.h"
#include "net.h"
#include "rtsp_protocol.h"
#include "rtsp_request_handler.h"
#include "rtsp_rtp_sender.h"
#include "rtsp_session_store.h"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char* kServiceName = "rtsp";
constexpr int64_t kRtspPeerAuthTtlMs = 10000;
constexpr uint32_t kRtspReaderDrainIntervalMs = 10;
constexpr uint32_t kRtspMaxFramesPerDrain = 8;
enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

struct RtspPeerAuthGrant {
    std::string peer_ip;
    StreamId stream_id = StreamId::kMain;
    std::string user_name;
    int64_t expires_at_ms = 0;
};

}  // namespace

using rtsp_internal::BasicRealmHeader;
using rtsp_internal::BuildRtspResponse;
using rtsp_internal::ContainsNoCase;
using rtsp_internal::CSeq;
using rtsp_internal::DecodeBase64;
using rtsp_internal::HeaderValue;
using rtsp_internal::ParseClientRtpPort;
using rtsp_internal::ParseRtspRequest;
using rtsp_internal::RtspRequest;
using rtsp_internal::StreamPath;

class RtspImpl : public IRtsp,
                 public IRtspRequestHandlerDelegate {
public:
    RtspImpl(RtspOptions options, RtspDependencies dependencies)
        : options_(std::move(options)),
          net_engine_(dependencies.net_engine),
          auth_(dependencies.auth),
          event_(dependencies.event),
          media_source_(dependencies.media_source),
          adaptive_observer_(dependencies.adaptive_observer),
          rtp_sender_(options_.rtp_mtu_bytes),
          request_handler_(this) {}

    ~RtspImpl() override {
        ReleaseInternal();
    }

    bool Prepare() {
        if (state_ == ServiceState::kInitialized ||
            state_ == ServiceState::kStarted ||
            state_ == ServiceState::kStopped) {
            return true;
        }
        if (net_engine_ == nullptr ||
            media_source_ == nullptr ||
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
        tcp_callbacks.on_accept = &RtspImpl::HandleAccept;
        tcp_callbacks.on_read = &RtspImpl::HandleRead;
        tcp_callbacks.on_close = &RtspImpl::HandleClose;
        TcpServerId server_result = net_engine_->ListenTcp(
            tcp_config, tcp_callbacks);
        if (server_result == 0) {
            return false;
        }
        server_id_ = server_result;

        NetAddress local_result = net_engine_->TcpLocalAddress(server_id_);
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
        std::vector<std::shared_ptr<RtspSession>> sessions;
        std::vector<ConnectionId> connection_ids;
        if (server_id_ != 0 && net_engine_ != nullptr) {
            (void)net_engine_->CloseTcp(server_id_);
            server_id_ = 0;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions = sessions_.Sessions();
            connection_ids = sessions_.ConnectionIds();
        }
        for (const auto &session : sessions) {
            CloseSessionResources(session,
                                  MediaFrameReaderCloseReason::kStreamStopped);
        }
        if (net_engine_ != nullptr) {
            for (ConnectionId connection_id : connection_ids) {
                (void)net_engine_->Close(connection_id);
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions_.Clear();
            peer_auth_grants_.clear();
        }
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
    const char* Name() const {
        return kServiceName;
    }

    RtspListenAddress LocalAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != ServiceState::kStarted) {
            return RtspListenAddress{};
        }
        return local_address_;
    }

    RtspStats GetStats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        RtspStats stats = stats_;
        stats.active_sessions = static_cast<uint32_t>(sessions_.Size());
        return stats;
    }

private:
    static void HandleAccept(void* user, ConnectionId id, NetAddress peer) {
        RtspImpl* self = static_cast<RtspImpl*>(user);
        if (self != nullptr) {
            self->OnConnection(id, std::move(peer));
        }
    }

    static void HandleRead(void* user,
                           ConnectionId id,
                           const uint8_t* data,
                           size_t size) {
        RtspImpl* self = static_cast<RtspImpl*>(user);
        if (self != nullptr) {
            self->OnMessage(id, data, static_cast<uint32_t>(size));
        }
    }

    static void HandleClose(void* user,
                            ConnectionId id,
                            TcpCloseReason reason) {
        RtspImpl* self = static_cast<RtspImpl*>(user);
        if (self != nullptr) {
            self->OnConnectionClosed(id, reason);
        }
    }

    static void HandleUdpRead(void* user,
                              UdpSocketId socket_id,
                              NetAddress peer,
                              const uint8_t* data,
                              size_t size) {
        RtspImpl* self = static_cast<RtspImpl*>(user);
        if (self != nullptr) {
            self->OnUdpPacket(socket_id, std::move(peer), data, size);
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
            (void)net_engine_->Close(connection_id);
            return;
        }
        Info("rtsp", "RTSP client connected conn=%llu peer=%s:%u",
                       static_cast<unsigned long long>(connection_id),
                       session->peer.ip.c_str(),
                       static_cast<unsigned>(session->peer.port));
        PublishEvent(EventType::kRtspClientConnected, session->peer.ip);
    }

    void OnConnectionClosed(ConnectionId id, TcpCloseReason reason) {
        std::shared_ptr<RtspSession> session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = sessions_.Remove(id);
        }
        if (!session) {
            return;
        }
        CloseSessionResources(session, MediaFrameReaderCloseReason::kDetached);
        Info("rtsp",
                       "RTSP client disconnected conn=%llu reason=%d "
                       "peer=%s:%u",
                       static_cast<unsigned long long>(id),
                       static_cast<int>(reason),
                       session->peer.ip.c_str(),
                       static_cast<unsigned>(session->peer.port));
        PublishEvent(EventType::kRtspClientDisconnected, session->peer.ip);
    }

    void OnUdpPacket(UdpSocketId socket_id,
                     NetAddress peer,
                     const uint8_t* data,
                     size_t size) {
        (void)peer;
        (void)data;
        (void)size;
        std::shared_ptr<RtspSession> session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = sessions_.FindByUdpSocket(socket_id);
        }
        if (session == nullptr) {
            return;
        }
        // RTCP is accepted to keep UDP clients' receiver reports harmless; RTP
        // is send-only for the current video preview scope.
    }

    void OnMessage(ConnectionId connection_id,
                   const uint8_t* data,
                   uint32_t size) {
        std::shared_ptr<RtspSession> session;
        RtspSplitterResult split;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = sessions_.Find(connection_id);
            if (!session) {
                (void)net_engine_->Close(connection_id);
                return;
            }
            if (!session->AppendBytes(data, size)) {
                return;
            }
            split = session->SplitRequests(options_.max_request_bytes);
        }

        if (split.status == RtspSplitterStatus::kPayloadTooLarge) {
            AddParseFailure();
            (void)net_engine_->Close(connection_id);
            return;
        }

        for (const std::string& raw : split.requests) {
            RtspRequest request;
            if (!ParseRtspRequest(raw, &request)) {
                AddParseFailure();
                SendResponse(connection_id, 400, "1", {}, "");
                (void)net_engine_->CloseAfterSend(connection_id);
                return;
            }
            request_handler_.HandleRequest(session, request);
        }
    }

    bool IsRtspStreamAvailable(StreamId stream_id) const override {
        return media_source_ != nullptr &&
               media_source_->IsStreamAvailable(stream_id);
    }

    MediaTrack RtspTrackForStream(StreamId stream_id) const override {
        MediaTrack track;
        track.stream_id = stream_id;
        track.codec = stream_id == StreamId::kSub ? options_.sub_video_codec
                                                  : options_.main_video_codec;
        track.clock_rate = media_mux::kRtpClockRate;
        if (media_source_ != nullptr &&
            media_source_->IsStreamAvailable(stream_id)) {
            track.codec = media_source_->GetStreamCodec(stream_id);
            track.ready = true;
        }
        return track;
    }

    bool AuthorizeRtspRequest(const std::shared_ptr<RtspSession>& session,
                              const RtspRequest& request,
                              StreamId stream_id) override {
        if (!options_.enable_auth) {
            return true;
        }
        if (auth_ == nullptr) {
            Error("rtsp", "RTSP auth service unavailable");
            SendResponse(session->connection_id, 500, CSeq(request), {}, "");
            return false;
        }
        if (session->IsAuthenticatedFor(stream_id)) {
            return true;
        }
        const std::string authorization = HeaderValue(request, "Authorization");
        const std::string prefix = "Basic ";
        if (authorization.compare(0, prefix.size(), prefix) != 0) {
            std::string cached_user_name;
            if (FindPeerAuthGrant(session->peer.ip, stream_id,
                                  &cached_user_name)) {
                session->MarkAuthenticated(stream_id, cached_user_name);
                Info("rtsp",
                               "RTSP auth reused peer=%s user=%s uri=%s",
                               session->peer.ip.c_str(),
                               cached_user_name.c_str(), request.uri.c_str());
                return true;
            }
            AddAuthFailure();
            Info("rtsp",
                           "RTSP auth required peer=%s uri=%s",
                           session->peer.ip.c_str(), request.uri.c_str());
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        std::string decoded;
        if (!DecodeBase64(authorization.substr(prefix.size()), &decoded)) {
            AddAuthFailure();
            Error("rtsp",
                            "RTSP auth invalid base64 peer=%s",
                            session->peer.ip.c_str());
            SendResponse(session->connection_id, 401, CSeq(request),
                         {{"WWW-Authenticate", BasicRealmHeader()}}, "");
            return false;
        }
        const size_t colon = decoded.find(':');
        if (colon == std::string::npos) {
            AddAuthFailure();
            Error("rtsp",
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
        LoginResult login_result = auth_->Login(login);
        if (login_result.token.empty()) {
            AddAuthFailure();
            Error("rtsp",
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
                auth_->Logout(logout_context));
            AddAuthFailure();
            Error("rtsp",
                            "RTSP auth rejected peer=%s user=%s "
                            "reason=must_change_password",
                            session->peer.ip.c_str(), login.user_name.c_str());
            SendResponse(session->connection_id, 403, CSeq(request), {}, "");
            return false;
        }
        const std::string target = StreamPath(stream_id);
        if (!auth_->CheckPermission(
                login_result.principal, AuthPermission::kPreviewVideo,
                target)) {
            static_cast<void>(
                auth_->Logout(logout_context));
            AddAuthFailure();
            Error("rtsp",
                            "RTSP auth forbidden peer=%s user=%s target=%s",
                            session->peer.ip.c_str(), login.user_name.c_str(),
                            target.c_str());
            SendResponse(session->connection_id, 403, CSeq(request), {}, "");
            return false;
        }
        static_cast<void>(auth_->Logout(logout_context));
        session->MarkAuthenticated(stream_id,
                                   login_result.principal.user_name);
        RememberPeerAuthGrant(session->peer.ip, stream_id,
                              login_result.principal.user_name);
        Info("rtsp",
                       "RTSP auth accepted peer=%s user=%s target=%s",
                       session->peer.ip.c_str(),
                       login_result.principal.user_name.c_str(),
                       target.c_str());
        return true;
    }

    bool SetupRtspTransport(const std::shared_ptr<RtspSession>& session,
                            const RtspRequest& request,
                            StreamId stream_id) override {
        const std::string transport = HeaderValue(request, "Transport");
        if (transport.empty()) {
            Error("rtsp", "RTSP setup missing Transport");
            SendResponse(session->connection_id, 461, CSeq(request), {}, "");
            return false;
        }
        std::string response_transport;
        if (ContainsNoCase(transport, "RTP/AVP/TCP") ||
            ContainsNoCase(transport, "interleaved")) {
            CloseSessionResources(session,
                                  MediaFrameReaderCloseReason::kDetached);
            session->SetupTcp(stream_id, 0);
            response_transport =
                "RTP/AVP/TCP;unicast;interleaved=0-1";
            AddTcpInterleavedSession();
        } else if (ContainsNoCase(transport, "RTP/AVP")) {
            const int client_port = ParseClientRtpPort(transport);
            if (client_port <= 0 || client_port > 65534) {
                Error(
                    "rtsp",
                    "RTSP setup unsupported UDP transport=%s",
                    transport.c_str());
                SendResponse(session->connection_id, 461, CSeq(request), {}, "");
                return false;
            }
            CloseSessionResources(session,
                                  MediaFrameReaderCloseReason::kDetached);
            UdpSocketId rtp_socket_id = 0;
            UdpSocketId rtcp_socket_id = 0;
            NetAddress server_rtp;
            NetAddress server_rtcp;
            if (!BindSessionUdpSockets(&rtp_socket_id, &rtcp_socket_id,
                                       &server_rtp, &server_rtcp)) {
                Error("rtsp", "RTSP setup UDP bind failed");
                SendResponse(session->connection_id, 461, CSeq(request), {}, "");
                return false;
            }
            session->SetupUdp(stream_id, rtp_socket_id, rtcp_socket_id,
                              static_cast<uint16_t>(client_port));
            response_transport = "RTP/AVP;unicast;client_port=" +
                                 std::to_string(client_port) + "-" +
                                 std::to_string(client_port + 1) +
                                 ";server_port=" +
                                 std::to_string(server_rtp.port) + "-" +
                                 std::to_string(server_rtcp.port);
            AddUdpSession();
        } else {
            Error("rtsp",
                            "RTSP setup unsupported transport=%s",
                            transport.c_str());
            SendResponse(session->connection_id, 461, CSeq(request), {}, "");
            return false;
        }
        RememberPeerAuthGrant(session->peer.ip, stream_id,
                              session->authenticated_user);
        Info("rtsp",
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
        return true;
    }

    bool BindSessionUdpSockets(UdpSocketId* rtp_socket_id,
                               UdpSocketId* rtcp_socket_id,
                               NetAddress* server_rtp,
                               NetAddress* server_rtcp) {
        if (rtp_socket_id == nullptr || rtcp_socket_id == nullptr ||
            server_rtp == nullptr || server_rtcp == nullptr ||
            net_engine_ == nullptr) {
            return false;
        }
        UdpBindOptions udp_config;
        udp_config.address = {options_.listen_ip, 0};
        UdpCallbacks udp_callbacks;
        udp_callbacks.user = this;
        udp_callbacks.on_read = &RtspImpl::HandleUdpRead;
        const UdpSocketId rtp_result = net_engine_->BindUdp(
            udp_config, udp_callbacks);
        if (rtp_result == 0) {
            return false;
        }
        const UdpSocketId rtcp_result = net_engine_->BindUdp(
            udp_config, udp_callbacks);
        if (rtcp_result == 0) {
            (void)net_engine_->CloseUdp(rtp_result);
            return false;
        }
        *server_rtp = net_engine_->UdpLocalAddress(rtp_result);
        *server_rtcp = net_engine_->UdpLocalAddress(rtcp_result);
        if (server_rtp->port == 0 || server_rtcp->port == 0) {
            (void)net_engine_->CloseUdp(rtp_result);
            (void)net_engine_->CloseUdp(rtcp_result);
            return false;
        }
        *rtp_socket_id = rtp_result;
        *rtcp_socket_id = rtcp_result;
        return true;
    }

    void SendRtspResponse(
        ConnectionId connection_id, int status, const std::string& cseq,
        const std::map<std::string, std::string>& headers,
        const std::string& body) override {
        const std::string response = BuildRtspResponse(status, cseq, headers, body);
        (void)net_engine_->Send(
            connection_id, reinterpret_cast<const uint8_t*>(response.data()),
            response.size());
    }

    void SendResponse(ConnectionId connection_id,
                      int status,
                      const std::string& cseq,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body) {
        SendRtspResponse(connection_id, status, cseq, headers, body);
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

    bool StartRtspPlayback(
        const std::shared_ptr<RtspSession>& session) override {
        if (session == nullptr || media_source_ == nullptr) {
            return false;
        }
        CloseSessionReader(session, MediaFrameReaderCloseReason::kDetached);
        MediaFrameReaderOptions reader_options;
        reader_options.stream_id = session->stream_id;
        reader_options.keyframe_first = true;
        reader_options.reader_name = kServiceName;
        const MediaFrameReaderId reader_id =
            media_source_->AttachFrameReader(reader_options);
        if (reader_id == 0) {
            return false;
        }
        MediaFrameReaderStartData start_data =
            media_source_->GetFrameReaderStartData(reader_id);
        if (!start_data.stream_running || !start_data.track.ready) {
            media_source_->DetachFrameReader(
                reader_id, MediaFrameReaderCloseReason::kDetached);
            MediaFrameReaderStartDataUnref(&start_data);
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session->StartPlaying();
            session->AttachReader(reader_id, start_data.reader_generation,
                                  start_data.track);
            session->SetStartFrames(&start_data.gop_frames);
        }
        NotifyAdaptive(*session, RtspAdaptiveEventType::kKeyFrameRequested);
        MediaFrameReaderStartDataUnref(&start_data);
        return true;
    }

    void ArmRtspPlayback(
        const std::shared_ptr<RtspSession>& session) override {
        ArmSessionDrainTimer(session);
    }

    void CloseRtspConnectionAfterSend(ConnectionId connection_id) override {
        std::shared_ptr<RtspSession> session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = sessions_.Find(connection_id);
        }
        CloseSessionResources(session, MediaFrameReaderCloseReason::kDetached);
        if (net_engine_ != nullptr) {
            (void)net_engine_->CloseAfterSend(connection_id);
        }
    }

    RtspListenAddress RtspLocalAddress() const override {
        return local_address_;
    }

    void PublishEvent(EventType type, const std::string& target) {
        if (event_ == nullptr) {
            return;
        }
        Event event;
        event.type = type;
        event.source = kServiceName;
        event.target = target;
        (void)event_->Publish(event);
    }

    void NotifyAdaptive(const RtspSession& session, RtspAdaptiveEventType event) {
        if (adaptive_observer_ == nullptr) {
            return;
        }
        RtspAdaptiveSample sample;
        sample.event = event;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sample.session = session.stats;
        }
        (void)adaptive_observer_->OnRtspAdaptiveSample(sample);
    }

    void ArmSessionDrainTimer(const std::shared_ptr<RtspSession>& session) {
        if (session == nullptr || net_engine_ == nullptr) {
            return;
        }
        const NetTimerId timer_id = net_engine_->RunOnIoEvery(
            kRtspReaderDrainIntervalMs, [this, session]() {
                DrainSessionFrames(session);
            });
        if (timer_id == 0) {
            MediaFrameReaderId reader_id = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                reader_id = session->reader_id;
                session->DetachReader();
            }
            (void)media_source_->DetachFrameReader(
                reader_id, MediaFrameReaderCloseReason::kDetached);
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        session->SetDrainTimer(timer_id);
    }

    void DrainSessionFrames(const std::shared_ptr<RtspSession>& session) {
        if (session == nullptr || media_source_ == nullptr) {
            return;
        }
        if (!FlushSessionStartFrames(session)) {
            return;
        }
        MediaFrameReaderId reader_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (session->state != RtspSessionState::kPlaying ||
                !session->HasReader()) {
                return;
            }
            reader_id = session->reader_id;
        }
        for (uint32_t i = 0; i < kRtspMaxFramesPerDrain; ++i) {
            MediaFrameReaderFrame reader_frame;
            if (!media_source_->PopFrameReaderFrame(reader_id,
                                                    &reader_frame)) {
                break;
            }
            SendMediaFrame(session, reader_frame.frame);
            MediaFrameReaderFrameUnref(&reader_frame);
        }
        MediaFrameReaderStatus status =
            media_source_->GetFrameReaderStatus(reader_id);
        if (status.attached && status.slow_reader) {
            NotifyAdaptive(*session, RtspAdaptiveEventType::kFrameDropped);
        }
    }

    bool FlushSessionStartFrames(const std::shared_ptr<RtspSession>& session) {
        while (true) {
            MediaFrame frame;
            bool has_frame = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (session == nullptr ||
                    session->state != RtspSessionState::kPlaying) {
                    return false;
                }
                if (!session->start_frames.empty()) {
                    (void)MediaFrameMove(&frame, &session->start_frames.front());
                    session->start_frames.erase(session->start_frames.begin());
                    has_frame = true;
                }
            }
            if (!has_frame) {
                return true;
            }
            SendMediaFrame(session, frame);
            MediaFrameUnref(&frame);
        }
    }

    void SendMediaFrame(const std::shared_ptr<RtspSession>& session,
                        const MediaFrame& frame) {
        bool should_send = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            should_send = session != nullptr &&
                          session->state == RtspSessionState::kPlaying &&
                          session->HasReader() &&
                          frame.stream_id == session->stream_id &&
                          frame.codec == session->track.codec;
        }
        if (should_send) {
            rtp_sender_.SendFrame(session, frame.encoded_frame,
                                  RtpSenderContext());
        }
    }

    void CloseSessionResources(
        const std::shared_ptr<RtspSession>& session,
        MediaFrameReaderCloseReason reason) {
        if (session == nullptr) {
            return;
        }
        CloseSessionReader(session, reason);
        CloseSessionUdp(session);
    }

    void CloseSessionUdp(const std::shared_ptr<RtspSession>& session) {
        if (session == nullptr) {
            return;
        }
        UdpSocketId rtp_socket_id = 0;
        UdpSocketId rtcp_socket_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            rtp_socket_id = session->rtp_socket_id;
            rtcp_socket_id = session->rtcp_socket_id;
            session->rtp_socket_id = 0;
            session->rtcp_socket_id = 0;
        }
        if (net_engine_ != nullptr) {
            if (rtp_socket_id != 0) {
                (void)net_engine_->CloseUdp(rtp_socket_id);
            }
            if (rtcp_socket_id != 0) {
                (void)net_engine_->CloseUdp(rtcp_socket_id);
            }
        }
    }

    void CloseSessionReader(const std::shared_ptr<RtspSession>& session,
                            MediaFrameReaderCloseReason reason) {
        if (session == nullptr) {
            return;
        }
        MediaFrameReaderId reader_id = 0;
        NetTimerId drain_timer_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            reader_id = session->reader_id;
            drain_timer_id = session->drain_timer_id;
            session->DetachReader();
            session->ClearDrainTimer();
        }
        if (net_engine_ != nullptr && drain_timer_id != 0) {
            (void)net_engine_->CancelIoTimer(drain_timer_id);
        }
        if (media_source_ != nullptr && reader_id != 0) {
            (void)media_source_->DetachFrameReader(reader_id, reason);
        }
    }

    RtspRtpSenderContext RtpSenderContext() {
        RtspRtpSenderContext context;
        context.net_engine = net_engine_;
        context.mutex = &mutex_;
        context.service_stats = &stats_;
        context.adaptive_observer = adaptive_observer_;
        return context;
    }

    RtspOptions options_;
    NetEngine* net_engine_ = nullptr;
    IAuth* auth_ = nullptr;
    IEvent* event_ = nullptr;
    IMediaFrameSource* media_source_ = nullptr;
    IRtspAdaptiveObserver* adaptive_observer_ = nullptr;
    RtspRtpSender rtp_sender_;
    RtspRequestHandler request_handler_;
    mutable std::mutex mutex_;
    ServiceState state_ = ServiceState::kCreated;
    TcpServerId server_id_ = 0;
    RtspListenAddress local_address_;
    RtspSessionStore sessions_;
    std::vector<RtspPeerAuthGrant> peer_auth_grants_;
    RtspStats stats_;
};

std::unique_ptr<IRtsp> CreateRtsp(
    const RtspOptions& options,
    const RtspDependencies& dependencies) {
    return std::unique_ptr<IRtsp>(
        new RtspImpl(options, dependencies));
}

const char* Rtsp::Name() {
    return kServiceName;
}

}  // namespace live_stream
