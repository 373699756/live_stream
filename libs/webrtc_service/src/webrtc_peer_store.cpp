#include "webrtc_peer_store.h"

#include "infra/time.h"

namespace live_stream {
namespace webrtc_internal {

WebrtcPeerInfo WebrtcPeerStore::CreatePeer(
    const WebrtcCreatePeerRequest &request, VideoCodec codec) {
  WebrtcPeerInfo peer;
  peer.peer_id = NextPeerId();
  peer.stream_id = request.stream_id;
  peer.codec = codec;
  peer.state = WebrtcPeerState::kCreated;
  peer.client_id = request.client_id;
  peer.session_id = request.session_id;
  peer.user_name = request.user_name;
  peer.client_ip = request.client_ip;
  peers_[peer.peer_id] = peer;
  Touch(peer.peer_id);
  return peer;
}

bool WebrtcPeerStore::Contains(const std::string &peer_id) const {
  return peers_.find(peer_id) != peers_.end();
}

WebrtcPeerInfo WebrtcPeerStore::GetPeer(const std::string &peer_id) const {
  const auto iter = peers_.find(peer_id);
  if (iter == peers_.end()) {
    return WebrtcPeerInfo();
  }
  return iter->second;
}

bool WebrtcPeerStore::RemovePeer(const std::string &peer_id) {
  const auto iter = peers_.find(peer_id);
  if (iter == peers_.end()) {
    return false;
  }
  peers_.erase(iter);
  peer_activity_ms_.erase(peer_id);
  pending_candidates_.erase(peer_id);
  return true;
}

void WebrtcPeerStore::Clear() {
  peers_.clear();
  peer_activity_ms_.clear();
  pending_candidates_.clear();
}

std::vector<std::string> WebrtcPeerStore::TakePeerIdsForClient(
    const std::string &session_id, const std::string &client_id) {
  std::vector<std::string> peer_ids;
  if (session_id.empty() && client_id.empty()) {
    return peer_ids;
  }
  const bool use_session = !session_id.empty();
  const bool use_client = !use_session && !client_id.empty();
  for (auto iter = peers_.begin(); iter != peers_.end();) {
    const WebrtcPeerInfo &peer = iter->second;
    const bool same_session = use_session && peer.session_id == session_id;
    const bool same_client = use_client &&
                             !peer.client_id.empty() &&
                             peer.client_id == client_id;
    if (same_session || same_client) {
      peer_ids.push_back(iter->first);
      pending_candidates_.erase(iter->first);
      peer_activity_ms_.erase(iter->first);
      iter = peers_.erase(iter);
      continue;
    }
    ++iter;
  }
  return peer_ids;
}

std::vector<std::string> WebrtcPeerStore::MarkAllClosing() {
  std::vector<std::string> peer_ids;
  for (auto &item : peers_) {
    item.second.state = WebrtcPeerState::kClosing;
    peer_ids.push_back(item.first);
  }
  peer_activity_ms_.clear();
  pending_candidates_.clear();
  return peer_ids;
}

uint32_t WebrtcPeerStore::ActivePeerCount() const {
  uint32_t count = 0;
  for (const auto &item : peers_) {
    if (IsOpenPeerState(item.second.state)) {
      ++count;
    }
  }
  return count;
}

bool WebrtcPeerStore::HasConnectedPeer(StreamId stream_id) const {
  for (const auto &item : peers_) {
    if (item.second.stream_id == stream_id &&
        item.second.state == WebrtcPeerState::kConnected) {
      return true;
    }
  }
  return false;
}

std::vector<WebrtcPeerInfo> WebrtcPeerStore::ConnectedPeers(
    StreamId stream_id) const {
  std::vector<WebrtcPeerInfo> peers;
  for (const auto &item : peers_) {
    if (item.second.stream_id == stream_id &&
        item.second.state == WebrtcPeerState::kConnected) {
      peers.push_back(item.second);
    }
  }
  return peers;
}

bool WebrtcPeerStore::BeginOffer(const std::string &peer_id,
                                 WebrtcPeerInfo *peer) {
  if (peer == nullptr) {
    return false;
  }
  auto iter = peers_.find(peer_id);
  if (iter == peers_.end() || !IsOpenPeerState(iter->second.state)) {
    return false;
  }
  iter->second.state = WebrtcPeerState::kOfferReceived;
  Touch(peer_id);
  *peer = iter->second;
  return true;
}

bool WebrtcPeerStore::CompleteOffer(
    const std::string &peer_id, WebrtcPeerInfo *peer,
    std::vector<WebrtcIceCandidate> *pending_candidates) {
  if (peer == nullptr || pending_candidates == nullptr) {
    return false;
  }
  auto iter = peers_.find(peer_id);
  if (iter == peers_.end()) {
    *peer = WebrtcPeerInfo();
    return false;
  }
  if (iter->second.state != WebrtcPeerState::kConnected) {
    iter->second.state = WebrtcPeerState::kConnecting;
  }
  auto candidate_iter = pending_candidates_.find(peer_id);
  if (candidate_iter != pending_candidates_.end()) {
    pending_candidates->swap(candidate_iter->second);
    pending_candidates_.erase(candidate_iter);
  }
  Touch(peer_id);
  *peer = iter->second;
  return true;
}

bool WebrtcPeerStore::AddOrQueueCandidate(const WebrtcIceCandidate &candidate,
                                          bool *queued) {
  if (queued == nullptr) {
    return false;
  }
  *queued = false;
  auto iter = peers_.find(candidate.peer_id);
  if (iter == peers_.end() || !IsOpenPeerState(iter->second.state)) {
    return false;
  }
  if (iter->second.state == WebrtcPeerState::kCreated ||
      iter->second.state == WebrtcPeerState::kOfferReceived) {
    pending_candidates_[candidate.peer_id].push_back(candidate);
    Touch(candidate.peer_id);
    *queued = true;
  }
  return true;
}

void WebrtcPeerStore::Touch(const std::string &peer_id) {
  peer_activity_ms_[peer_id] = NowMs();
}

bool WebrtcPeerStore::MarkClosing(const std::string &peer_id) {
  auto iter = peers_.find(peer_id);
  if (iter == peers_.end()) {
    return false;
  }
  iter->second.state = WebrtcPeerState::kClosing;
  return true;
}

EnginePeerStateUpdate WebrtcPeerStore::ApplyEngineState(
    const std::string &peer_id, WebrtcPeerState state) {
  EnginePeerStateUpdate update;
  auto iter = peers_.find(peer_id);
  if (iter == peers_.end()) {
    return update;
  }
  update.found = true;
  switch (state) {
    case WebrtcPeerState::kConnecting:
      if (IsOpenPeerState(iter->second.state) &&
          iter->second.state != WebrtcPeerState::kConnected) {
        iter->second.state = WebrtcPeerState::kConnecting;
        Touch(peer_id);
      }
      break;
    case WebrtcPeerState::kConnected:
      if (iter->second.state != WebrtcPeerState::kConnected) {
        iter->second.state = WebrtcPeerState::kConnected;
        update.stream_id = iter->second.stream_id;
        update.request_key_frame = true;
      }
      Touch(peer_id);
      break;
    case WebrtcPeerState::kFailed:
      iter->second.state = WebrtcPeerState::kFailed;
      RemovePeer(peer_id);
      break;
    case WebrtcPeerState::kClosed:
    case WebrtcPeerState::kClosing:
      iter->second.state = WebrtcPeerState::kClosed;
      RemovePeer(peer_id);
      break;
    case WebrtcPeerState::kCreated:
    case WebrtcPeerState::kOfferReceived:
      break;
  }
  return update;
}

bool WebrtcPeerStore::GetOpenPeerStream(const std::string &peer_id,
                                        StreamId *stream_id) const {
  if (stream_id == nullptr) {
    return false;
  }
  const auto iter = peers_.find(peer_id);
  if (iter == peers_.end() || !IsOpenPeerState(iter->second.state)) {
    return false;
  }
  *stream_id = iter->second.stream_id;
  return true;
}

std::vector<std::string> WebrtcPeerStore::TakeStaleSetupPeerIds(
    int64_t timeout_ms) {
  std::vector<std::string> peer_ids;
  const int64_t now_ms = NowMs();
  for (auto iter = peers_.begin(); iter != peers_.end();) {
    auto activity_iter = peer_activity_ms_.find(iter->first);
    if (activity_iter == peer_activity_ms_.end()) {
      activity_iter =
          peer_activity_ms_.insert(std::make_pair(iter->first, now_ms)).first;
    }
    if (IsSetupPeerState(iter->second.state) &&
        now_ms - activity_iter->second >= timeout_ms) {
      peer_ids.push_back(iter->first);
      pending_candidates_.erase(iter->first);
      peer_activity_ms_.erase(iter->first);
      iter = peers_.erase(iter);
    } else {
      ++iter;
    }
  }
  return peer_ids;
}

bool WebrtcPeerStore::IsOpenPeerState(WebrtcPeerState state) {
  return state != WebrtcPeerState::kClosing &&
         state != WebrtcPeerState::kClosed &&
         state != WebrtcPeerState::kFailed;
}

bool WebrtcPeerStore::IsSetupPeerState(WebrtcPeerState state) {
  return state == WebrtcPeerState::kCreated ||
         state == WebrtcPeerState::kOfferReceived ||
         state == WebrtcPeerState::kConnecting;
}

int64_t WebrtcPeerStore::NowMs() { return infra::Time::MonotonicMillis(); }

std::string WebrtcPeerStore::NextPeerId() {
  std::string id = "webrtc-";
  id += std::to_string(next_peer_id_++);
  return id;
}

}  // namespace webrtc_internal
}  // namespace live_stream
