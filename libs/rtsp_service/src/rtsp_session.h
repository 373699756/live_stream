#ifndef LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_SESSION_H_
#define LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_SESSION_H_

#include "net_service.h"
#include "rtsp_service.h"
#include "rtsp_splitter.h"

#include <cstdint>
#include <string>

namespace live_stream {

enum class RtspSessionState {
  kInit = 0,
  kReady,
  kPlaying,
  kClosed,
};

class RtspSession {
 public:
  RtspSession(ConnectionId connection_id, NetAddress peer,
              uint64_t session_id);

  bool AppendBytes(const uint8_t *data, uint32_t size);
  RtspSplitterResult SplitRequests(uint32_t max_request_bytes);
  bool IsReadyForPlay() const;

  void MarkDescribed(StreamId stream_id);
  void SetupTcp(StreamId stream_id, uint8_t interleaved_rtp_channel);
  void SetupUdp(StreamId stream_id, uint16_t client_rtp_port);
  void StartPlaying();
  void Close();
  void MarkAuthenticated(StreamId stream_id, std::string user_name);
  bool IsAuthenticatedFor(StreamId stream_id) const;

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
  RtspSessionStats stats;

 private:
  RtspSplitter splitter_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_SESSION_H_
