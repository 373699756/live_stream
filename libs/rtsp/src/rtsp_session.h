#ifndef LIVE_STREAM_RTSP_SRC_RTSP_SESSION_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_SESSION_H_

#include "media_source.h"
#include "net.h"
#include "rtsp.h"
#include "rtsp_splitter.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {

enum class RtspSessionState {
  // 初始状态：只接收 OPTIONS/DESCRIBE/SETUP，不允许 PLAY。
  kInit = 0,
  // SETUP 完成，transport 已绑定，PLAY 可以创建 reader。
  kReady,
  // PLAY 成功，drain timer 正在把 media_source reader 的帧转成 RTP。
  kPlaying,
  // TEARDOWN 或连接关闭后的终态；资源清理由 RtspImpl 统一执行。
  kClosed,
};

class RtspSession {
 public:
  RtspSession(ConnectionId connection_id, NetAddress peer,
              uint64_t session_id);
  ~RtspSession();

  bool AppendBytes(const uint8_t *data, uint32_t size);
  RtspSplitterResult SplitRequests(uint32_t max_request_bytes);
  bool IsReadyForPlay() const;

  void MarkDescribed(StreamId stream_id);
  void SetupTcp(StreamId stream_id, uint8_t interleaved_rtp_channel);
  void SetupUdp(StreamId stream_id, UdpSocketId rtp_socket_id,
                UdpSocketId rtcp_socket_id, uint16_t client_rtp_port);
  void StartPlaying();
  void AttachReader(MediaFrameReaderId next_reader_id,
                    uint64_t reader_generation, MediaTrack next_track);
  void DetachReader();
  bool HasReader() const;
  void SetStartFrames(std::vector<MediaFrame> *frames);
  void ClearStartFrames();
  void SetDrainTimer(NetTimerId timer_id);
  void ClearDrainTimer();
  void Close();
  void MarkCloseReason(TcpCloseReason reason);
  void MarkAuthenticated(StreamId stream_id, std::string user_name);
  bool IsAuthenticatedFor(StreamId stream_id) const;
  void RecordRtcpPacket(size_t packet_size, int64_t now_ms);

  uint64_t session_id = 0;
  ConnectionId connection_id = 0;
  NetAddress peer;
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
  // RTP sequence/ssrc 是每个 RTSP session 独立的发送状态。
  uint16_t rtp_sequence = 1;
  uint32_t ssrc = 0;
  MediaTrack track;
  // PLAY 后必须先发送关键帧；未看到关键帧前的非关键帧会在 RtspRtpSender 丢弃。
  bool keyframe_seen = false;
  MediaFrameReaderId reader_id = 0;
  uint64_t reader_generation = 0;
  // start_frames 是 PLAY 时抓取的启动 GOP，只在第一次 drain 时发送并释放。
  std::vector<MediaFrame> start_frames;
  NetTimerId drain_timer_id = 0;
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
