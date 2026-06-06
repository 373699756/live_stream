#include "rtsp_session_store.h"

#include <utility>

namespace live_stream {

bool RtspSessionStore::Add(ConnectionId connection_id, NetAddress peer,
                           uint32_t max_sessions,
                           std::shared_ptr<RtspSession> *session) {
  if (session == nullptr || sessions_.size() >= max_sessions) {
    return false;
  }
  std::shared_ptr<RtspSession> next(
      new RtspSession(connection_id, std::move(peer), next_session_id_++));
  sessions_[connection_id] = next;
  *session = next;
  return true;
}

std::shared_ptr<RtspSession> RtspSessionStore::Find(
    ConnectionId connection_id) const {
  const auto iter = sessions_.find(connection_id);
  if (iter == sessions_.end()) {
    return nullptr;
  }
  return iter->second;
}

std::shared_ptr<RtspSession> RtspSessionStore::Remove(
    ConnectionId connection_id) {
  const auto iter = sessions_.find(connection_id);
  if (iter == sessions_.end()) {
    return nullptr;
  }
  std::shared_ptr<RtspSession> session = iter->second;
  sessions_.erase(iter);
  return session;
}

std::vector<std::shared_ptr<RtspSession>> RtspSessionStore::PlayingTargets(
    StreamId stream_id) const {
  std::vector<std::shared_ptr<RtspSession>> targets;
  for (const auto &entry : sessions_) {
    const std::shared_ptr<RtspSession> &session = entry.second;
    if (session && session->state == RtspSessionState::kPlaying &&
        session->stream_id == stream_id) {
      targets.push_back(session);
    }
  }
  return targets;
}

void RtspSessionStore::Clear() { sessions_.clear(); }

size_t RtspSessionStore::Size() const { return sessions_.size(); }

}  // namespace live_stream
