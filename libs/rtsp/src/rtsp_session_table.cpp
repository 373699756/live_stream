#include "rtsp_session_table.h"

#include <utility>

namespace live_stream {

bool RtspSessionTable::Add(ConnectionId connection_id, NetAddress peer,
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

std::shared_ptr<RtspSession> RtspSessionTable::Find(
    ConnectionId connection_id) const {
    const auto iter = sessions_.find(connection_id);
    if (iter == sessions_.end()) {
        return nullptr;
    }
    return iter->second;
}

std::shared_ptr<RtspSession> RtspSessionTable::FindByUdpSocket(
    UdpSocketId socket_id) const {
    if (socket_id == 0) {
        return nullptr;
    }
    for (const auto &entry : sessions_) {
        const std::shared_ptr<RtspSession> &session = entry.second;
        if (session && (session->rtp_socket_id == socket_id ||
                        session->rtcp_socket_id == socket_id)) {
            return session;
        }
    }
    return nullptr;
}

std::shared_ptr<RtspSession> RtspSessionTable::Remove(
    ConnectionId connection_id) {
    const auto iter = sessions_.find(connection_id);
    if (iter == sessions_.end()) {
        return nullptr;
    }
    std::shared_ptr<RtspSession> session = iter->second;
    sessions_.erase(iter);
    return session;
}

std::vector<ConnectionId> RtspSessionTable::ConnectionIds() const {
    std::vector<ConnectionId> ids;
    for (const auto &entry : sessions_) {
        ids.push_back(entry.first);
    }
    return ids;
}

std::vector<std::shared_ptr<RtspSession>> RtspSessionTable::Sessions() const {
    std::vector<std::shared_ptr<RtspSession>> sessions;
    for (const auto &entry : sessions_) {
        if (entry.second) {
            sessions.push_back(entry.second);
        }
    }
    return sessions;
}

void RtspSessionTable::Clear() { sessions_.clear(); }

size_t RtspSessionTable::Size() const { return sessions_.size(); }

}  // namespace live_stream
