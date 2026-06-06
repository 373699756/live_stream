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
  stream_id = next_stream_id;
  state = RtspSessionState::kReady;
  transport = RtspTransportMode::kTcpInterleaved;
  interleaved_rtp_channel = next_interleaved_rtp_channel;
  stats.transport = transport;
  stats.stream_id = stream_id;
}

void RtspSession::SetupUdp(StreamId next_stream_id,
                           uint16_t next_client_rtp_port) {
  stream_id = next_stream_id;
  state = RtspSessionState::kReady;
  transport = RtspTransportMode::kUdp;
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

void RtspSession::Close() {
  state = RtspSessionState::kClosed;
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

}  // namespace live_stream
