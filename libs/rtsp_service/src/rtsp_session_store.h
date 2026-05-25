#ifndef LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_SESSION_STORE_H_
#define LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_SESSION_STORE_H_

#include "net_service.h"
#include "rtsp_service.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

enum class RtspSessionState {
  kInit = 0,
  kReady,
  kPlaying,
  kClosed,
};

struct RtspSession {
  uint64_t session_id = 0;
  ConnectionId connection_id = 0;
  NetAddress peer;
  RtspSessionState state = RtspSessionState::kInit;
  StreamId stream_id = StreamId::kMain;
  RtspTransportMode transport = RtspTransportMode::kTcpInterleaved;
  uint8_t interleaved_rtp_channel = 0;
  uint16_t client_rtp_port = 0;
  uint16_t rtp_sequence = 1;
  uint32_t ssrc = 0;
  bool keyframe_seen = false;
  bool authenticated = false;
  StreamId authenticated_stream_id = StreamId::kMain;
  std::string authenticated_user;
  std::string request_buffer;
  RtspSessionStats stats;
};

// RtspSessionStore owns RTSP connection/session indexing. It is intentionally
// not internally synchronized; RtspServiceImpl protects it with its mutex.
class RtspSessionStore {
 public:
  bool Add(ConnectionId connection_id, NetAddress peer, uint32_t max_sessions,
           std::shared_ptr<RtspSession> *session);
  std::shared_ptr<RtspSession> Find(ConnectionId connection_id) const;
  std::shared_ptr<RtspSession> Remove(ConnectionId connection_id);
  std::vector<std::shared_ptr<RtspSession>> PlayingTargets(
      StreamId stream_id) const;
  void Clear();
  size_t Size() const;

 private:
  std::map<ConnectionId, std::shared_ptr<RtspSession>> sessions_;
  uint64_t next_session_id_ = 1;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_SESSION_STORE_H_
