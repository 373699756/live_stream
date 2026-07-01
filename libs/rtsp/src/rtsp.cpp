#include "rtsp.h"

#include "event.h"
#include "infra/log.h"
#include "infra/time.h"
#include "media/media_source_registry.h"
#include "media/media_streams.h"
#include "socket_io.h"
#include "rtsp_auth.h"
#include "rtsp_protocol.h"
#include "rtsp_request_handler.h"
#include "rtsp_session_close.h"
#include "rtsp_session_event.h"
#include "rtsp_session_table.h"
#include "rtsp_session_video_sender.h"
#include "runtime.h"

#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char* kProtocolName = "rtsp";
enum class RtspPhase {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

std::string AddressText(const SocketAddress& address) {
    if (address.ip.empty() || address.port == 0) {
        return std::string();
    }
    return address.ip + ":" + std::to_string(address.port);
}

std::string Hex32(uint32_t value) {
    std::ostringstream output;
    output << std::hex << std::nouppercase << std::setw(8)
           << std::setfill('0') << value;
    return output.str();
}

}  // namespace

using rtsp_internal::BasicRealmHeader;
using rtsp_internal::BuildRtspResponse;
using rtsp_internal::ContainsNoCase;
using rtsp_internal::CSeq;
using rtsp_internal::HeaderValue;
using rtsp_internal::IRtspAuthResponder;
using rtsp_internal::ParseClientRtpPort;
using rtsp_internal::ParseRtspRequest;
using rtsp_internal::RtspAuth;
using rtsp_internal::RtspRequest;
using rtsp_internal::StreamPath;

class RtspImpl : public IRtsp,
                 public IRtspRequestHandlerDelegate,
                 public IRtspAuthResponder {
public:
    RtspImpl(RtspOptions options, event::Loop *socket_loop)
        : options_(std::move(options)),
          socket_io_(Runtime::SocketIo()),
          net_loop_(socket_loop),
          auth_(Runtime::Auth()),
          event_(Runtime::EventCenter()),
          media_streams_(MediaSourceRegistry::Streams()),
          session_video_sender_(media_streams_, net_loop_, socket_io_,
                                &mutex_, &stats_, options_.rtp_mtu_bytes),
          session_close_(socket_io_, &mutex_, &session_video_sender_),
          session_event_(event_, &mutex_, &session_table_, kProtocolName),
          rtsp_auth_(auth_, *this),
          request_handler_(*this) {}

    ~RtspImpl() override {
        ReleaseInternal();
    }

    bool Prepare() {
        if (phase_ == RtspPhase::kInitialized ||
            phase_ == RtspPhase::kStarted ||
            phase_ == RtspPhase::kStopped) {
            return true;
        }
        if (socket_io_ == nullptr ||
            net_loop_ == nullptr ||
            media_streams_ == nullptr ||
            options_.max_sessions == 0 || options_.rtp_mtu_bytes < 64 ||
            options_.max_request_bytes == 0) {
            return false;
        }
        phase_ = RtspPhase::kInitialized;
        return true;
    }

    bool Start() override {
        if (phase_ == RtspPhase::kStarted) {
            return true;
        }
        if (phase_ == RtspPhase::kCreated ||
            phase_ == RtspPhase::kDeinitialized) {
            if (!Prepare()) {
                return false;
            }
        }
        if (phase_ != RtspPhase::kInitialized &&
            phase_ != RtspPhase::kStopped) {
            return false;
        }

        TcpListenOptions tcp_config;
        tcp_config.address = {options_.listen_ip, options_.listen_port};
        tcp_config.owner_protocol = kProtocolName;
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
        TcpServerId server_result = socket_io_->ListenTcp(
            net_loop_, tcp_config, tcp_callbacks);
        if (server_result == 0) {
            return false;
        }
        server_id_ = server_result;

        SocketAddress local_result = socket_io_->TcpLocalAddress(server_id_);
        local_address_ = {options_.listen_ip,
                          local_result.port != 0 ? local_result.port
                                                 : options_.listen_port};
        phase_ = RtspPhase::kStarted;
        return true;
    }

    void Stop() override {
        StopInternal();
    }

    bool ApplyOptions(const RtspOptions& options) override {
        if (!CanApplyOptions(options)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        options_.enable_auth = options.enable_auth;
        options_.main_video_codec = options.main_video_codec;
        options_.sub_video_codec = options.sub_video_codec;
        return true;
    }

    void Release() {
        ReleaseInternal();
    }

private:
    void StopInternal() {
        std::vector<std::shared_ptr<RtspSession>> sessions;
        std::vector<ConnectionId> connection_ids;
        if (server_id_ != 0) {
            (void)socket_io_->CloseTcp(server_id_);
            server_id_ = 0;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions = session_table_.Sessions();
            connection_ids = session_table_.ConnectionIds();
        }
        // 停服务时先停 listener，再关闭每个 session 的 subscription/timer/UDP socket，
        // 最后关闭 TCP 控制连接，防止 media_streams 继续给已关闭 transport 推帧。
        for (const auto& session : sessions) {
            session_close_.CloseSessionVideoSend(
                *session, SubscriptionClose::kStreamStopped);
        }
        for (ConnectionId connection_id : connection_ids) {
            (void)socket_io_->Close(connection_id);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session_table_.Clear();
        }
        rtsp_auth_.Clear();
        if (phase_ == RtspPhase::kStarted ||
            phase_ == RtspPhase::kInitialized) {
            phase_ = RtspPhase::kStopped;
        }
    }

    bool CanApplyOptions(const RtspOptions& options) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return options.listen_ip == options_.listen_ip &&
               options.listen_port == options_.listen_port &&
               options.max_sessions == options_.max_sessions &&
               options.max_request_bytes == options_.max_request_bytes &&
               options.session_timeout_ms == options_.session_timeout_ms &&
               options.rtp_mtu_bytes == options_.rtp_mtu_bytes &&
               options.send_queue_capacity == options_.send_queue_capacity &&
               options.send_buffer_limit_bytes ==
                   options_.send_buffer_limit_bytes &&
               options.send_stall_timeout_ms ==
                   options_.send_stall_timeout_ms &&
               options.default_transport == options_.default_transport;
    }

    void ReleaseInternal() {
        StopInternal();
        if (phase_ != RtspPhase::kCreated) {
            phase_ = RtspPhase::kDeinitialized;
        }
    }

public:
    const char* Name() const {
        return kProtocolName;
    }

    RtspListenAddress LocalAddress() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (phase_ != RtspPhase::kStarted) {
            return RtspListenAddress{};
        }
        return local_address_;
    }

    RtspStats GetStats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        RtspStats stats = stats_;
        stats.active_sessions = static_cast<uint32_t>(session_table_.Size());
        return stats;
    }

    std::vector<RtspSessionInfo>
    ListSessionInfo() const override {
        std::vector<std::shared_ptr<RtspSession>> sessions;
        RtspListenAddress local_address;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions = session_table_.Sessions();
            local_address = local_address_;
        }

        std::vector<RtspSessionInfo> session_info;
        session_info.reserve(sessions.size());
        for (const std::shared_ptr<RtspSession>& session : sessions) {
            RtspSessionInfo item;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                item.session_id = session->session_id;
                item.stream_id = session->stream_id;
                item.transport = session->transport;
                item.remote_address = AddressText(session->peer);
                item.subscription_id = session->subscription_id;
                item.pending_bytes = session->stats.pending_bytes;
                item.rtp_packets = session->stats.sent_rtp_packets;
                item.rtp_bytes = session->stats.sent_rtp_bytes;
                item.rtcp_packets = session->stats.received_rtcp_packets;
                item.rtcp_bytes = session->stats.received_rtcp_bytes;
                item.last_rtcp_ms = session->stats.last_rtcp_ms;
                item.close_reason = TcpCloseReasonName(session->close_reason);
            }
            const SocketConnectionInfo connection_info =
                socket_io_->GetConnectionInfo(session->connection_id);
            const bool has_connection_info =
                connection_info.connection_id == session->connection_id;
            if (has_connection_info &&
                !connection_info.local_address.ip.empty() &&
                connection_info.local_address.port != 0) {
                item.local_address =
                    AddressText(connection_info.local_address);
            }
            if (has_connection_info && item.pending_bytes == 0) {
                item.pending_bytes = connection_info.pending_bytes;
            }
            if (has_connection_info) {
                item.close_reason =
                    TcpCloseReasonName(connection_info.close_reason);
            }
            if (item.subscription_id != 0) {
                const SubscriptionInfo subscription_info =
                    media_streams_->GetSubscriptionInfo(
                        item.subscription_id);
                item.subscription_open = subscription_info.open;
                if (subscription_info.open) {
                    item.subscription_generation =
                        subscription_info.generation;
                    item.subscription_pending_frames =
                        subscription_info.pending_frames;
                    item.subscription_waiting_keyframe =
                        subscription_info.wait_keyframe;
                    item.subscription_slow = subscription_info.slow;
                    item.subscription_close_reason =
                        SubscriptionCloseName(
                            subscription_info.close_reason);
                }
            }
            if (item.local_address.empty()) {
                item.local_address =
                    local_address.ip + ":" +
                    std::to_string(local_address.port);
            }
            session_info.push_back(std::move(item));
        }
        return session_info;
    }

private:
    static void HandleAccept(void* user, ConnectionId id, SocketAddress peer) {
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
                              SocketAddress peer,
                              const uint8_t* data,
                              size_t size) {
        RtspImpl* self = static_cast<RtspImpl*>(user);
        if (self != nullptr) {
            self->OnUdpPacket(socket_id, std::move(peer), data, size);
        }
    }

    void OnConnection(ConnectionId connection_id, SocketAddress peer) {
        std::shared_ptr<RtspSession> session;
        bool accepted = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (session_table_.Add(connection_id, std::move(peer),
                                   options_.max_sessions, session)) {
                ++stats_.total_sessions;
                accepted = true;
            }
        }
        if (!accepted) {
            (void)socket_io_->Close(connection_id);
            return;
        }
        Info("rtsp", "RTSP client connected conn=%llu peer=%s:%u",
             static_cast<unsigned long long>(connection_id),
             session->peer.ip.c_str(),
             static_cast<unsigned>(session->peer.port));
        session_event_.Publish(event::EventType::kRtspClientConnected,
                               session->peer.ip);
    }

    void OnConnectionClosed(ConnectionId id, TcpCloseReason reason) {
        std::shared_ptr<RtspSession> session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = session_table_.Remove(id);
        }
        if (!session) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session->MarkCloseReason(reason);
        }
        session_close_.CloseSessionVideoSend(
            *session, SubscriptionClose::kUnsubscribed);
        Info("rtsp",
             "RTSP client disconnected conn=%llu reason=%d "
             "peer=%s:%u",
             static_cast<unsigned long long>(id),
             static_cast<int>(reason),
             session->peer.ip.c_str(),
             static_cast<unsigned>(session->peer.port));
        session_event_.Publish(event::EventType::kRtspClientDisconnected,
                               session->peer.ip);
    }

    void OnUdpPacket(UdpSocketId socket_id,
                     SocketAddress peer,
                     const uint8_t* data,
                     size_t size) {
        (void)data;
        std::shared_ptr<RtspSession> session;
        bool learned_rtp_peer = false;
        bool learned_rtcp_peer = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = session_table_.FindByUdpSocket(socket_id);
            if (session == nullptr || peer.ip != session->peer.ip) {
                return;
            }
            if (session->rtp_socket_id == socket_id) {
                learned_rtp_peer = session->LearnUdpRtpPeer(peer);
            } else if (session->rtcp_socket_id == socket_id) {
                learned_rtcp_peer = session->LearnUdpRtcpPeer(peer);
                session->RecordRtcpPacket(size,
                                          infra::Time::MonotonicMillis());
            }
        }
        if (learned_rtp_peer || learned_rtcp_peer) {
            Info("rtsp",
                 "RTSP UDP peer learned conn=%llu type=%s peer=%s:%u",
                 static_cast<unsigned long long>(session->connection_id),
                 learned_rtp_peer ? "rtp" : "rtcp", peer.ip.c_str(),
                 static_cast<unsigned>(peer.port));
        }
        // 当前产品只发视频预览 RTP，不根据 RTCP receiver report 做码率控制。
        // 这里接收并忽略 RTCP，避免客户端上报包触发解析错误或断连。
    }

    void OnMessage(ConnectionId connection_id,
                   const uint8_t* data,
                   uint32_t size) {
        std::shared_ptr<RtspSession> session;
        RtspSplitterResult split;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = session_table_.Find(connection_id);
            if (!session) {
                (void)socket_io_->Close(connection_id);
                return;
            }
            if (!session->AppendBytes(data, size)) {
                return;
            }
            split = session->SplitRequests(options_.max_request_bytes);
        }

        if (split.status == RtspSplitterStatus::kPayloadTooLarge) {
            AddParseFailure();
            // request 超过上限时不尝试返回部分响应，直接断开控制连接。
            (void)socket_io_->Close(connection_id);
            return;
        }

        // splitter 同时能切 RTSP request 和 TCP interleaved frame；当前只记录
        // RTCP 反馈用于诊断，不根据 receiver report 做码率控制。
        RecordInterleavedRtcpPackets(*session, split.interleaved_packets);
        for (const std::string& raw : split.requests) {
            RtspRequest request;
            if (!ParseRtspRequest(raw, &request)) {
                AddParseFailure();
                // 解析失败仍带一个兜底 CSeq，方便部分客户端把错误归到当前请求；
                // 响应排队后关闭，避免继续处理错乱的控制连接。
                SendResponse(connection_id, 400, "1", {}, "");
                (void)socket_io_->CloseAfterSend(connection_id);
                return;
            }
            request_handler_.HandleRequest(session, request);
        }
    }

    void RecordInterleavedRtcpPackets(
        RtspSession& session,
        const std::vector<RtspInterleavedPacket>& packets) {
        if (packets.empty()) {
            return;
        }
        const int64_t now_ms = infra::Time::MonotonicMillis();
        std::lock_guard<std::mutex> lock(mutex_);
        const uint8_t rtcp_channel = session.interleaved_rtp_channel + 1;
        for (const RtspInterleavedPacket& packet : packets) {
            if (session.transport == RtspTransportMode::kTcpInterleaved &&
                packet.channel == rtcp_channel) {
                session.RecordRtcpPacket(packet.payload.size(), now_ms);
            }
        }
    }

    bool IsRtspStreamAvailable(StreamId stream_id) const override {
        return media_streams_->IsStreamAvailable(stream_id);
    }

    MediaStreamInfo RtspStreamInfoForStream(StreamId stream_id) const override {
        if (!media_streams_->IsStreamAvailable(stream_id)) {
            return MediaStreamInfo{};
        }
        return media_streams_->GetStreamInfo(stream_id);
    }

    bool AuthorizeRtspRequest(RtspSession& session,
                              const RtspRequest& request,
                              StreamId stream_id) override {
        if (!options_.enable_auth) {
            return true;
        }
        return rtsp_auth_.Authorize(session, request, stream_id);
    }

    bool SetupRtspTransport(RtspSession& session,
                            const RtspRequest& request,
                            StreamId stream_id) override {
        const std::string transport = HeaderValue(request, "Transport");
        if (transport.empty()) {
            Error("rtsp", "RTSP setup missing Transport");
            SendResponse(session.connection_id, 461, CSeq(request), {}, "");
            return false;
        }
        std::string response_transport;
        if (ContainsNoCase(transport, "RTP/AVP/TCP") ||
            ContainsNoCase(transport, "interleaved")) {
            // 重新 SETUP 会切换 transport，必须先清掉旧 subscription 和 UDP socket，
            // 否则旧 transport 仍可能收到发送 timer 推送。
            session_close_.CloseSessionVideoSend(
                session, SubscriptionClose::kUnsubscribed);
            session.SetupTcp(stream_id, 0);
            response_transport =
                "RTP/AVP/TCP;unicast;interleaved=0-1;ssrc=" +
                Hex32(session.ssrc);
            AddTcpInterleavedSession();
        } else if (ContainsNoCase(transport, "RTP/AVP")) {
            const int client_port = ParseClientRtpPort(transport);
            if (client_port <= 0 || client_port > 65534) {
                Error(
                    "rtsp",
                    "RTSP setup unsupported UDP transport=%s",
                    transport.c_str());
                SendResponse(session.connection_id, 461, CSeq(request), {}, "");
                return false;
            }
            // UDP 每个 session 独立绑定本地 RTP/RTCP 端口，响应里的 server_port
            // 必须来自实际 bind 结果，不能用配置端口推导。
            session_close_.CloseSessionVideoSend(
                session, SubscriptionClose::kUnsubscribed);
            UdpSocketId rtp_socket_id = 0;
            UdpSocketId rtcp_socket_id = 0;
            SocketAddress server_rtp;
            SocketAddress server_rtcp;
            if (!BindSessionUdpSockets(rtp_socket_id, rtcp_socket_id,
                                       server_rtp, server_rtcp)) {
                Error("rtsp", "RTSP setup UDP bind failed");
                SendResponse(session.connection_id, 461, CSeq(request), {}, "");
                return false;
            }
            session.SetupUdp(stream_id, rtp_socket_id, rtcp_socket_id,
                             static_cast<uint16_t>(client_port));
            response_transport = "RTP/AVP/UDP;unicast;client_port=" +
                                 std::to_string(client_port) + "-" +
                                 std::to_string(client_port + 1) +
                                 ";server_port=" +
                                 std::to_string(server_rtp.port) + "-" +
                                 std::to_string(server_rtcp.port) +
                                 ";ssrc=" + Hex32(session.ssrc);
            AddUdpSession();
        } else {
            Error("rtsp",
                  "RTSP setup unsupported transport=%s",
                  transport.c_str());
            SendResponse(session.connection_id, 461, CSeq(request), {}, "");
            return false;
        }
        Info("rtsp",
             "RTSP setup conn=%llu stream=%s transport=%s",
             static_cast<unsigned long long>(
                 session.connection_id),
             StreamPath(stream_id),
             session.transport == RtspTransportMode::kTcpInterleaved
                 ? "tcp"
                 : "udp");
        SendResponse(session.connection_id, 200, CSeq(request),
                     {{"Transport", response_transport},
                      {"Session", std::to_string(session.session_id)}},
                     "");
        return true;
    }

    bool BindSessionUdpSockets(UdpSocketId &rtp_socket_id,
                               UdpSocketId &rtcp_socket_id,
                               SocketAddress &server_rtp,
                               SocketAddress &server_rtcp) {
        UdpBindOptions udp_config;
        udp_config.address = {options_.listen_ip, 0};
        UdpCallbacks udp_callbacks;
        udp_callbacks.user = this;
        udp_callbacks.on_read = &RtspImpl::HandleUdpRead;
        const UdpSocketId rtp_result = socket_io_->BindUdp(
            net_loop_, udp_config, udp_callbacks);
        if (rtp_result == 0) {
            return false;
        }
        const UdpSocketId rtcp_result = socket_io_->BindUdp(
            net_loop_, udp_config, udp_callbacks);
        if (rtcp_result == 0) {
            (void)socket_io_->CloseUdp(rtp_result);
            return false;
        }
        server_rtp = socket_io_->UdpLocalAddress(rtp_result);
        server_rtcp = socket_io_->UdpLocalAddress(rtcp_result);
        if (server_rtp.port == 0 || server_rtcp.port == 0) {
            (void)socket_io_->CloseUdp(rtp_result);
            (void)socket_io_->CloseUdp(rtcp_result);
            return false;
        }
        rtp_socket_id = rtp_result;
        rtcp_socket_id = rtcp_result;
        return true;
    }

    void SendRtspResponse(
        ConnectionId connection_id, int status, const std::string& cseq,
        const std::map<std::string, std::string>& headers,
        const std::string& body) override {
        const std::string response = BuildRtspResponse(status, cseq, headers, body);
        (void)socket_io_->Send(
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

    void SendAuthResponse(
        ConnectionId connection_id, int status, const std::string& cseq,
        const std::map<std::string, std::string>& headers,
        const std::string& body) override {
        SendResponse(connection_id, status, cseq, headers, body);
    }

    void AddTcpInterleavedSession() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.tcp_interleaved_sessions;
    }

    void AddUdpSession() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.udp_sessions;
    }

    int StartRtspMediaStream(RtspSession& session) override {
        if (!media_streams_->IsStreamAvailable(session.stream_id)) {
            return 404;
        }
        return session_video_sender_.StartMediaStream(session);
    }

    void StartRtspMediaSend(
        const std::shared_ptr<RtspSession>& session) override {
        session_video_sender_.StartMediaSend(session);
    }

    void CloseRtspConnectionAfterSend(ConnectionId connection_id) override {
        std::shared_ptr<RtspSession> session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = session_table_.Find(connection_id);
        }
        if (session != nullptr) {
            session_close_.CloseSessionVideoSend(
                *session, SubscriptionClose::kUnsubscribed);
        }
        (void)socket_io_->CloseAfterSend(connection_id);
    }

    RtspListenAddress RtspLocalAddress() const override {
        return local_address_;
    }

    RtspOptions options_;
    ISocketIo* socket_io_ = nullptr;
    event::Loop* net_loop_ = nullptr;
    IAuth* auth_ = nullptr;
    event::EventCenter* event_ = nullptr;
    MediaStreams* media_streams_ = nullptr;
    mutable std::mutex mutex_;
    RtspSessionTable session_table_;
    RtspStats stats_;
    RtspSessionVideoSender session_video_sender_;
    RtspSessionClose session_close_;
    RtspSessionEvent session_event_;
    RtspAuth rtsp_auth_;
    RtspRequestHandler request_handler_;
    RtspPhase phase_ = RtspPhase::kCreated;
    TcpServerId server_id_ = 0;
    RtspListenAddress local_address_;
};

std::unique_ptr<IRtsp> CreateRtsp(
    const RtspOptions& options,
    event::Loop* socket_loop) {
    return std::unique_ptr<IRtsp>(
        new RtspImpl(options, socket_loop));
}

const char* Rtsp::Name() {
    return kProtocolName;
}

}  // namespace live_stream
