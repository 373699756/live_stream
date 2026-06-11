#include "rtsp.h"

#include "event.h"
#include "infra/log.h"
#include "infra/time.h"
#include "rtp.h"
#include "net.h"
#include "rtsp_auth.h"
#include "rtsp_protocol.h"
#include "rtsp_request_handler.h"
#include "rtsp_rtp_sender.h"
#include "rtsp_session_store.h"

#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char* kServiceName = "rtsp";
constexpr uint32_t kRtspReaderDrainIntervalMs = 10;
constexpr uint32_t kRtspMaxFramesPerDrain = 8;
enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopped,
    kDeinitialized,
};

std::string AddressText(const NetAddress &address) {
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

uint32_t FirstStartFrameRtpTimestamp(
    const FrameSubscriptionStartData &start_data) {
    if (start_data.gop_frames.empty()) {
        return 0;
    }
    return rtp::RtpTimestampFromPtsUs(
        start_data.gop_frames.front().pts_us);
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
    RtspImpl(RtspOptions options, RtspDependencies dependencies)
        : options_(std::move(options)),
          net_engine_(dependencies.net_engine),
          net_executor_(dependencies.net_executor),
          auth_(dependencies.auth),
          event_(dependencies.event),
          media_streams_(dependencies.media_streams),
          rtp_sender_(options_.rtp_mtu_bytes),
          rtsp_auth_(auth_, this),
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
            net_executor_ == nullptr ||
            media_streams_ == nullptr ||
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
        tcp_config.owner_protocol = kServiceName;
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
        if (net_executor_ == nullptr) {
            return false;
        }
        TcpServerId server_result = net_engine_->ListenTcp(
            net_executor_, tcp_config, tcp_callbacks);
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
        if (server_id_ != 0 && net_engine_ != nullptr) {
            (void)net_engine_->CloseTcp(server_id_);
            server_id_ = 0;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions = sessions_.Sessions();
            connection_ids = sessions_.ConnectionIds();
        }
        // 停服务时先停 listener，再关闭每个 session 的 subscription/timer/UDP socket，
        // 最后关闭 TCP 控制连接，防止 media_streams 继续给已关闭 transport 推帧。
        for (const auto &session : sessions) {
            CloseSessionResources(
                session, FrameSubscriptionCloseReason::kStreamStopped);
        }
        if (net_engine_ != nullptr) {
            for (ConnectionId connection_id : connection_ids) {
                (void)net_engine_->Close(connection_id);
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions_.Clear();
        }
        rtsp_auth_.Clear();
        if (state_ == ServiceState::kStarted ||
            state_ == ServiceState::kInitialized) {
            state_ = ServiceState::kStopped;
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

    std::vector<RtspSessionDiagnostics>
    GetSessionDiagnostics() const override {
        std::vector<std::shared_ptr<RtspSession>> sessions;
        RtspListenAddress local_address;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions = sessions_.Sessions();
            local_address = local_address_;
        }

        std::vector<RtspSessionDiagnostics> diagnostics;
        diagnostics.reserve(sessions.size());
        for (const std::shared_ptr<RtspSession> &session : sessions) {
            if (session == nullptr) {
                continue;
            }
            RtspSessionDiagnostics item;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                item.session_id = session->session_id;
                item.stream_id = session->stream_id;
                item.transport = session->transport;
                item.remote_address = AddressText(session->peer);
                item.reader_id = session->subscription_id;
                item.pending_bytes = session->stats.pending_bytes;
                item.rtp_packets = session->stats.sent_rtp_packets;
                item.rtp_bytes = session->stats.sent_rtp_bytes;
                item.rtcp_packets = session->stats.received_rtcp_packets;
                item.rtcp_bytes = session->stats.received_rtcp_bytes;
                item.last_rtcp_ms = session->stats.last_rtcp_ms;
                item.close_reason = TcpCloseReasonName(session->close_reason);
            }
            if (net_engine_ != nullptr) {
                const NetConnectionDiagnostics net_diagnostics =
                    net_engine_->GetConnectionDiagnostics(
                        session->connection_id);
                const bool has_net_diagnostics =
                    net_diagnostics.connection_id == session->connection_id;
                if (has_net_diagnostics &&
                    !net_diagnostics.local_address.ip.empty() &&
                    net_diagnostics.local_address.port != 0) {
                    item.local_address =
                        AddressText(net_diagnostics.local_address);
                }
                if (has_net_diagnostics && item.pending_bytes == 0) {
                    item.pending_bytes = net_diagnostics.pending_bytes;
                }
                if (has_net_diagnostics) {
                    item.close_reason =
                        TcpCloseReasonName(net_diagnostics.close_reason);
                }
            }
            if (media_streams_ != nullptr && item.reader_id != 0) {
                const FrameSubscriptionInfo reader_status =
                    media_streams_->GetFrameSubscriptionInfo(
                        item.reader_id);
                item.reader_attached = reader_status.attached;
                if (reader_status.attached) {
                    item.reader_generation =
                        reader_status.subscription_generation;
                    item.reader_pending_frames =
                        reader_status.pending_frames;
                    item.reader_waiting_keyframe =
                        reader_status.waiting_for_keyframe;
                    item.reader_slow = reader_status.slow_subscriber;
                    item.reader_close_reason =
                        FrameSubscriptionCloseReasonName(
                            reader_status.close_reason);
                }
            }
            if (item.local_address.empty()) {
                item.local_address =
                    local_address.ip + ":" +
                    std::to_string(local_address.port);
            }
            diagnostics.push_back(std::move(item));
        }
        return diagnostics;
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
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session->MarkCloseReason(reason);
        }
        CloseSessionResources(session,
                              FrameSubscriptionCloseReason::kUnsubscribed);
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
        (void)data;
        std::shared_ptr<RtspSession> session;
        bool learned_rtp_peer = false;
        bool learned_rtcp_peer = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = sessions_.FindByUdpSocket(socket_id);
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
        if (session == nullptr) {
            return;
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
            // request 超过上限时不尝试返回部分响应，直接断开控制连接。
            (void)net_engine_->Close(connection_id);
            return;
        }

        // splitter 同时能切 RTSP request 和 TCP interleaved frame；当前只记录
        // RTCP 反馈用于诊断，不根据 receiver report 做码率控制。
        RecordInterleavedRtcpPackets(session, split.interleaved_packets);
        for (const std::string& raw : split.requests) {
            RtspRequest request;
            if (!ParseRtspRequest(raw, &request)) {
                AddParseFailure();
                // 解析失败仍带一个兜底 CSeq，方便部分客户端把错误归到当前请求；
                // 响应排队后关闭，避免继续处理错乱的控制连接。
                SendResponse(connection_id, 400, "1", {}, "");
                (void)net_engine_->CloseAfterSend(connection_id);
                return;
            }
            request_handler_.HandleRequest(session, request);
        }
    }

    void RecordInterleavedRtcpPackets(
        const std::shared_ptr<RtspSession> &session,
        const std::vector<RtspInterleavedPacket> &packets) {
        if (session == nullptr || packets.empty()) {
            return;
        }
        const int64_t now_ms = infra::Time::MonotonicMillis();
        std::lock_guard<std::mutex> lock(mutex_);
        const uint8_t rtcp_channel = session->interleaved_rtp_channel + 1;
        for (const RtspInterleavedPacket &packet : packets) {
            if (session->transport == RtspTransportMode::kTcpInterleaved &&
                packet.channel == rtcp_channel) {
                session->RecordRtcpPacket(packet.payload.size(), now_ms);
            }
        }
    }

    bool IsRtspStreamAvailable(StreamId stream_id) const override {
        return media_streams_ != nullptr &&
               media_streams_->IsStreamAvailable(stream_id);
    }

    MediaStreamInfo RtspStreamInfoForStream(StreamId stream_id) const override {
        if (media_streams_ == nullptr ||
            !media_streams_->IsStreamAvailable(stream_id)) {
            return MediaStreamInfo{};
        }
        return media_streams_->GetStreamInfo(stream_id);
    }

    bool AuthorizeRtspRequest(const std::shared_ptr<RtspSession>& session,
                              const RtspRequest& request,
                              StreamId stream_id) override {
        if (!options_.enable_auth) {
            return true;
        }
        return rtsp_auth_.Authorize(session, request, stream_id);
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
            // 重新 SETUP 会切换 transport，必须先清掉旧 subscription 和 UDP socket，
            // 否则旧 transport 仍可能收到 drain timer 推送。
            CloseSessionResources(session,
                                  FrameSubscriptionCloseReason::kUnsubscribed);
            session->SetupTcp(stream_id, 0);
            response_transport =
                "RTP/AVP/TCP;unicast;interleaved=0-1;ssrc=" +
                Hex32(session->ssrc);
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
            // UDP 每个 session 独立绑定本地 RTP/RTCP 端口，响应里的 server_port
            // 必须来自实际 bind 结果，不能用配置端口推导。
            CloseSessionResources(session,
                                  FrameSubscriptionCloseReason::kUnsubscribed);
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
            response_transport = "RTP/AVP/UDP;unicast;client_port=" +
                                 std::to_string(client_port) + "-" +
                                 std::to_string(client_port + 1) +
                                 ";server_port=" +
                                 std::to_string(server_rtp.port) + "-" +
                                 std::to_string(server_rtcp.port) +
                                 ";ssrc=" + Hex32(session->ssrc);
            AddUdpSession();
        } else {
            Error("rtsp",
                            "RTSP setup unsupported transport=%s",
                            transport.c_str());
            SendResponse(session->connection_id, 461, CSeq(request), {}, "");
            return false;
        }
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
            net_executor_, udp_config, udp_callbacks);
        if (rtp_result == 0) {
            return false;
        }
        const UdpSocketId rtcp_result = net_engine_->BindUdp(
            net_executor_, udp_config, udp_callbacks);
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

    void SendAuthResponse(
        ConnectionId connection_id, int status, const std::string &cseq,
        const std::map<std::string, std::string> &headers,
        const std::string &body) override {
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

    int StartRtspPlayback(
        const std::shared_ptr<RtspSession>& session) override {
        if (session == nullptr || media_streams_ == nullptr) {
            return 500;
        }
        if (!media_streams_->IsStreamAvailable(session->stream_id)) {
            return 404;
        }
        CloseSessionSubscription(session,
                                 FrameSubscriptionCloseReason::kUnsubscribed);
        // PLAY 才创建长期 subscription，keyframe_first 让媒体链路优先给关键帧，
        // 并把当前 GOP 作为 start frames 返回给本 session。
        FrameSubscriptionOptions subscription_options;
        subscription_options.stream_id = session->stream_id;
        subscription_options.keyframe_first = true;
        subscription_options.subscriber_name = kServiceName;
        const FrameSubscriptionId subscription_id =
            media_streams_->SubscribeFrames(subscription_options);
        if (subscription_id == 0) {
            return 455;
        }
        FrameSubscriptionStartData start_data =
            media_streams_->GetFrameSubscriptionStartData(subscription_id);
        if (!start_data.stream_info.track_ready) {
            // subscription 创建成功但启动数据不可用，必须立刻 unsubscribe，
            // 避免空 subscription 长期占用 media_streams。
            media_streams_->UnsubscribeFrames(
                subscription_id, FrameSubscriptionCloseReason::kUnsubscribed);
            FrameSubscriptionStartDataUnref(&start_data);
            return 455;
        }
        const uint32_t play_rtp_timestamp =
            FirstStartFrameRtpTimestamp(start_data);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session->StartPlaying();
            session->AttachSubscription(subscription_id,
                                        start_data.subscription_generation,
                                        start_data.stream_info);
            session->SetPlayRtpTimestamp(play_rtp_timestamp);
            session->SetStartFrames(&start_data.gop_frames);
        }
        FrameSubscriptionStartDataUnref(&start_data);
        return 200;
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
        CloseSessionResources(session,
                              FrameSubscriptionCloseReason::kUnsubscribed);
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

    void ArmSessionDrainTimer(const std::shared_ptr<RtspSession>& session) {
        if (session == nullptr || net_engine_ == nullptr) {
            return;
        }
        // drain timer 运行在 net IO loop 上，周期性从 media_streams subscription 拉帧；
        // 每次 drain 有帧数上限，避免单个 RTSP 客户端长期占住 IO 线程。
        if (net_executor_ == nullptr) {
            return;
        }
        const NetTimerId timer_id = net_executor_->RunEvery(
            kRtspReaderDrainIntervalMs, [this, session]() {
                DrainSessionFrames(session);
            });
        if (timer_id == 0) {
            FrameSubscriptionId subscription_id = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                subscription_id = session->subscription_id;
                session->DetachSubscription();
            }
            // timer 创建失败时不能继续保留 subscription，否则没有 drain 消费帧队列。
            (void)media_streams_->UnsubscribeFrames(
                subscription_id, FrameSubscriptionCloseReason::kUnsubscribed);
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        session->SetDrainTimer(timer_id);
    }

    void DrainSessionFrames(const std::shared_ptr<RtspSession>& session) {
        if (session == nullptr || media_streams_ == nullptr) {
            return;
        }
        if (!FlushSessionStartFrames(session)) {
            return;
        }
        FrameSubscriptionId subscription_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (session->state != RtspSessionState::kPlaying ||
                !session->HasSubscription()) {
                return;
            }
            subscription_id = session->subscription_id;
        }
        for (uint32_t i = 0; i < kRtspMaxFramesPerDrain; ++i) {
            SubscribedFrame subscribed_frame;
            if (!media_streams_->PopSubscribedFrame(subscription_id,
                                                    &subscribed_frame)) {
                break;
            }
            // Pop 出来的 frame 带引用，发送路径只在本次调用内使用；
            // SendMediaFrame 返回后必须 unref。
            SendMediaFrame(session, subscribed_frame.frame);
            SubscribedFrameUnref(&subscribed_frame);
        }
    }

    bool FlushSessionStartFrames(const std::shared_ptr<RtspSession>& session) {
        while (true) {
            EncodedFrame frame;
            bool has_frame = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (session == nullptr ||
                    session->state != RtspSessionState::kPlaying) {
                    return false;
                }
                if (!session->start_frames.empty()) {
                    // Move 后 vector 中的原 frame 被置空，锁外发送可以缩短 RTSP mutex
                    // 持有时间，避免发送慢客户端时阻塞其它控制请求。
                    (void)EncodedFrameMove(&frame, &session->start_frames.front());
                    session->start_frames.erase(session->start_frames.begin());
                    has_frame = true;
                }
            }
            if (!has_frame) {
                return true;
            }
            SendMediaFrame(session, frame);
            EncodedFrameUnref(&frame);
        }
    }

    void SendMediaFrame(const std::shared_ptr<RtspSession>& session,
                        const EncodedFrame& frame) {
        bool should_send = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            should_send = session != nullptr &&
                          session->state == RtspSessionState::kPlaying &&
                          session->HasSubscription() &&
                          frame.stream_id == session->stream_id &&
                          frame.codec == session->stream_info.codec;
        }
        if (should_send) {
            // SendFrame 内部会再次读取 transport 和统计字段；这里先过滤 stream/codec，
            // 防止旧 subscription 或错误码流的数据进入当前 session。
            rtp_sender_.SendFrame(session, frame,
                                  RtpSenderContext());
        }
    }

    void CloseSessionResources(
        const std::shared_ptr<RtspSession>& session,
        FrameSubscriptionCloseReason reason) {
        if (session == nullptr) {
            return;
        }
        CloseSessionSubscription(session, reason);
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
        // socket id 清零后再关闭 net endpoint，避免关闭回调里再次找到同一 session
        // 并重复关闭相同 UDP socket。
        if (net_engine_ != nullptr) {
            if (rtp_socket_id != 0) {
                (void)net_engine_->CloseUdp(rtp_socket_id);
            }
            if (rtcp_socket_id != 0) {
                (void)net_engine_->CloseUdp(rtcp_socket_id);
            }
        }
    }

    void CloseSessionSubscription(const std::shared_ptr<RtspSession>& session,
                                  FrameSubscriptionCloseReason reason) {
        if (session == nullptr) {
            return;
        }
        FrameSubscriptionId subscription_id = 0;
        NetTimerId drain_timer_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            subscription_id = session->subscription_id;
            drain_timer_id = session->drain_timer_id;
            session->DetachSubscription();
            session->ClearDrainTimer();
        }
        // 先取消 drain timer，再 unsubscribe subscription。timer 若已在执行，
        // EventLoop 的 cancelled 标记会阻止下一次触发；subscription_id 清零后
        // 本次执行也会快速退出。
        if (net_executor_ != nullptr && drain_timer_id != 0) {
            (void)net_executor_->CancelTimer(drain_timer_id);
        }
        if (media_streams_ != nullptr && subscription_id != 0) {
            (void)media_streams_->UnsubscribeFrames(subscription_id, reason);
        }
    }

    RtspRtpSenderContext RtpSenderContext() {
        RtspRtpSenderContext context;
        context.net_engine = net_engine_;
        context.mutex = &mutex_;
        context.service_stats = &stats_;
        return context;
    }

    RtspOptions options_;
    INetEngine* net_engine_ = nullptr;
    INetExecutor *net_executor_ = nullptr;
    IAuth* auth_ = nullptr;
    IEvent* event_ = nullptr;
    MediaStreams* media_streams_ = nullptr;
    RtspRtpSender rtp_sender_;
    RtspAuth rtsp_auth_;
    RtspRequestHandler request_handler_;
    mutable std::mutex mutex_;
    ServiceState state_ = ServiceState::kCreated;
    TcpServerId server_id_ = 0;
    RtspListenAddress local_address_;
    RtspSessionStore sessions_;
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
