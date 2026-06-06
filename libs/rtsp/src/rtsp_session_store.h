#ifndef LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_SESSION_STORE_H_
#define LIVE_STREAM_RTSP_SERVICE_SRC_RTSP_SESSION_STORE_H_

#include "rtsp_session.h"

#include <map>
#include <memory>
#include <vector>

namespace live_stream {

// RtspSessionStore owns RTSP connection/session indexing. It is intentionally
// not internally synchronized; RtspServiceImpl protects it with its mutex.
class RtspSessionStore {
 public:
  bool Add(ConnectionId connection_id, NetAddress peer, uint32_t max_sessions,
           std::shared_ptr<RtspSession> *session);
  std::shared_ptr<RtspSession> Find(ConnectionId connection_id) const;
  std::shared_ptr<RtspSession> Remove(ConnectionId connection_id);
  std::vector<ConnectionId> ConnectionIds() const;
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
