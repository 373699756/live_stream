#include "rtsp_session.h"

#include <utility>

namespace live_stream {
namespace {

constexpr uint32_t kDefaultSsrcBase = 0x52545350;

}  // namespace

RtspSession::RtspSession(ConnectionId next_connection_id,
                         NetAddress next_peer, uint64_t next_session_id)
    : session_id(next_session_id),
      connection_id(next_connection_id),
      peer(std::move(next_peer)) {
  ssrc = kDefaultSsrcBase ^ static_cast<uint32_t>(session_id);
  stats.session_id = session_id;
}

RtspSession::~RtspSession() {
  ClearStartFrames();
}

bool RtspSession::AppendBytes(const uint8_t *data, uint32_t size) {
  return splitter_.Append(data, size);
}

RtspSplitterResult RtspSession::SplitRequests(uint32_t max_request_bytes) {
  return splitter_.Split(max_request_bytes);
}

bool RtspSession::IsReadyForPlay() const {
  return state == RtspSessionState::kReady ||
         state == RtspSessionState::kPlaying;
}

void RtspSession::MarkDescribed(StreamId next_stream_id) {
  stream_id = next_stream_id;
}

void RtspSession::SetupTcp(StreamId next_stream_id,
                           uint8_t next_interleaved_rtp_channel) {
  // SETUP 只把控制连接推进到 Ready；真正的 media reader 在 PLAY 时创建，
  // 避免客户端只 SETUP 不 PLAY 时占用帧队列。
  stream_id = next_stream_id;
  state = RtspSessionState::kReady;
  transport = RtspTransportMode::kTcpInterleaved;
  interleaved_rtp_channel = next_interleaved_rtp_channel;
  rtp_socket_id = 0;
  rtcp_socket_id = 0;
  client_rtp_port = 0;
  stats.transport = transport;
  stats.stream_id = stream_id;
}

void RtspSession::SetupUdp(StreamId next_stream_id, UdpSocketId next_rtp_socket_id,
                           UdpSocketId next_rtcp_socket_id,
                           uint16_t next_client_rtp_port) {
  // UDP transport 归单个 RTSP session 所有，TEARDOWN、断连或重新 SETUP 时必须
  // 关闭这两个 socket。
  stream_id = next_stream_id;
  state = RtspSessionState::kReady;
  transport = RtspTransportMode::kUdp;
  rtp_socket_id = next_rtp_socket_id;
  rtcp_socket_id = next_rtcp_socket_id;
  client_rtp_port = next_client_rtp_port;
  stats.transport = transport;
  stats.stream_id = stream_id;
}

void RtspSession::StartPlaying() {
  state = RtspSessionState::kPlaying;
  keyframe_seen = false;
  stats.stream_id = stream_id;
  stats.transport = transport;
}

void RtspSession::AttachReader(MediaFrameReaderId next_reader_id,
                               uint64_t next_reader_generation,
                               MediaTrack next_track) {
  // reader_generation 来自 media_source，用于后续诊断和防止旧 reader 状态混入
  // 新一轮播放；RTSP 自己不维护 GOP cache。
  reader_id = next_reader_id;
  reader_generation = next_reader_generation;
  track = std::move(next_track);
  keyframe_seen = false;
}

void RtspSession::DetachReader() {
  reader_id = 0;
  reader_generation = 0;
  track = MediaTrack{};
  keyframe_seen = false;
  play_rtp_timestamp = 0;
  ClearStartFrames();
}

bool RtspSession::HasReader() const {
  return reader_id != 0;
}

void RtspSession::SetStartFrames(std::vector<MediaFrame> *frames) {
  // Start frames 是 PLAY 时 media_source 返回的启动 GOP，只在首轮 drain 中发送，
  // 发送后立即 unref，不在 RTSP session 中长期缓存。
  ClearStartFrames();
  if (frames != nullptr) {
    start_frames.swap(*frames);
  }
}

void RtspSession::SetPlayRtpTimestamp(uint32_t timestamp) {
  play_rtp_timestamp = timestamp;
}

void RtspSession::ClearStartFrames() {
  for (MediaFrame &frame : start_frames) {
    MediaFrameUnref(&frame);
  }
  start_frames.clear();
}

void RtspSession::SetDrainTimer(NetTimerId timer_id) {
  drain_timer_id = timer_id;
}

void RtspSession::ClearDrainTimer() {
  drain_timer_id = 0;
}

void RtspSession::Close() {
  state = RtspSessionState::kClosed;
}

void RtspSession::MarkCloseReason(TcpCloseReason reason) {
  close_reason = reason;
}

void RtspSession::MarkAuthenticated(StreamId next_stream_id,
                                    std::string user_name) {
  authenticated = true;
  authenticated_stream_id = next_stream_id;
  authenticated_user = std::move(user_name);
}

bool RtspSession::IsAuthenticatedFor(StreamId next_stream_id) const {
  return authenticated && authenticated_stream_id == next_stream_id;
}

void RtspSession::RecordRtcpPacket(size_t packet_size, int64_t now_ms) {
  if (packet_size == 0) {
    return;
  }
  ++stats.received_rtcp_packets;
  stats.received_rtcp_bytes += packet_size;
  stats.last_rtcp_ms = now_ms;
}

}  // namespace live_stream
