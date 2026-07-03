#ifndef LIVE_STREAM_RTSP_SRC_RTSP_SESSION_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_SESSION_H_

#include "media/media_streams.h"
#include "socket_io.h"
#include "rtsp.h"
#include "rtsp_splitter.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace live_stream {

enum class RtspSessionState {
    // 初始状态：只接收 OPTIONS/DESCRIBE/SETUP，不允许 PLAY。
    kInit = 0,
    // SETUP 完成，transport 已绑定，PLAY 可以创建 subscription。
    kReady,
    // PLAY 成功，drain timer 正在把 media_streams subscription 的帧转成 RTP。
    kPlaying,
    // TEARDOWN 或连接关闭后的终态；资源清理由 RtspImpl 统一执行。
    kClosed,
};

class RtspSession {
public:
    RtspSession(ConnectionId connection_id, SocketAddress peer,
                uint64_t session_id);

    bool AppendBytes(const uint8_t *data, uint32_t size);
    RtspSplitterResult SplitRequests(uint32_t max_request_bytes);
    bool IsReadyForPlay() const;

    void MarkDescribed(StreamId stream_id);
    void SetupTcp(StreamId stream_id, uint8_t interleaved_rtp_channel);
    void SetupUdp(StreamId stream_id, UdpSocketId rtp_socket_id,
                  UdpSocketId rtcp_socket_id, uint16_t client_rtp_port);
    bool LearnUdpRtpPeer(SocketAddress next_udp_rtp_peer);
    bool LearnUdpRtcpPeer(SocketAddress next_udp_rtcp_peer);
    void StartPlaying();
    void SetSubscription(FrameSubscriptionId next_subscription_id,
                         uint64_t next_subscription_generation,
                         MediaStreamInfo next_stream_info);
    void ClearSubscription();
    bool IsSubscribed() const;
    void SetStartFrames(std::vector<MediaFrame> *frames);
    void SetPlayRtpTimestamp(uint32_t timestamp);
    void ClearStartFrames();
    void SetDrainTimer(event::TimerId timer_id);
    void ClearDrainTimer();
    void Close();
    void MarkCloseReason(TcpCloseReason reason);
    void MarkAuthenticated(StreamId stream_id, std::string user_name);
    bool IsAuthenticatedFor(StreamId stream_id) const;
    void RecordRtcpPacket(size_t packet_size, int64_t now_ms);

    uint64_t session_id = 0;
    ConnectionId connection_id = 0;
    SocketAddress peer;
    RtspSessionState state = RtspSessionState::kInit;
    // stream_id 在 DESCRIBE/SETUP 阶段确定；PLAY 不再从 URI 改写它。
    StreamId stream_id = StreamId::kMain;
    RtspTransportMode transport = RtspTransportMode::kTcpInterleaved;
    // TCP interleaved 使用控制连接承载 RTP，当前只使用 RTP channel 0。
    uint8_t interleaved_rtp_channel = 0;
    // UDP 模式下这两个 socket 归 session 独占，CloseSessionUdp() 负责关闭。
    UdpSocketId rtp_socket_id = 0;
    UdpSocketId rtcp_socket_id = 0;
    uint16_t client_rtp_port = 0;
    // UDP RTP/RTCP 发送目标先来自 SETUP client_port；收到客户端 UDP 打洞包后
    // 更新为实际来源地址和端口，匹配 ZLMediaKit 的 NAT peer 学习行为。
    SocketAddress udp_rtp_peer;
    SocketAddress udp_rtcp_peer;
    // RTP sequence/ssrc 是每个 RTSP session 独立的发送状态。
    uint16_t rtp_sequence = 1;
    uint32_t ssrc = 0;
    uint32_t play_rtp_timestamp = 0;
    int64_t last_rtcp_sender_report_ms = 0;
    MediaStreamInfo stream_info;
    // PLAY 后必须先发送关键帧；未看到关键帧前的非关键帧会在 RtspRtpSender 丢弃。
    bool keyframe_seen = false;
    FrameSubscriptionId subscription_id = 0;
    uint64_t subscription_generation = 0;
    // start_frames 是 PLAY 时抓取的启动 GOP，只在第一次 drain 时发送并释放。
    std::deque<MediaFrame> start_frames;
    event::TimerId drain_timer_id = 0;
    // Basic auth 成功只缓存到本 RTSP session，不持有 auth token。
    bool authenticated = false;
    StreamId authenticated_stream_id = StreamId::kMain;
    std::string authenticated_user;
    TcpCloseReason close_reason = TcpCloseReason::kNormal;
    RtspSessionStats stats;

private:
    RtspSplitter splitter_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_SESSION_H_
