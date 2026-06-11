#include "webrtc_peer_table.h"

#include "infra/time.h"

namespace live_stream {
namespace webrtc_internal {

WebrtcPeerInfo WebrtcPeerTable::CreatePeer(
    const WebrtcCreatePeerRequest &request, Codec codec) {
  WebrtcPeerInfo peer;
  peer.peer_id = NextPeerId();
  peer.stream_id = request.stream_id;
  peer.codec = codec;
  peer.state = WebrtcPeerState::kCreated;
  peer.client_id = request.client_id;
  peer.session_id = request.session_id;
  peer.user_name = request.user_name;
  peer.client_ip = request.client_ip;
  peer.created_at_ms = NowMs();
  peer.updated_at_ms = peer.created_at_ms;
  peers_[peer.peer_id] = peer;
  Touch(peer.peer_id);
  return peer;
}

bool WebrtcPeerTable::Contains(const std::string &peer_id) const {
  return peers_.find(peer_id) != peers_.end();
}

WebrtcPeerInfo WebrtcPeerTable::GetPeer(const std::string &peer_id) const {
  const auto iter = peers_.find(peer_id);
  if (iter == peers_.end()) {
    return WebrtcPeerInfo();
  }
  return iter->second;
}

bool WebrtcPeerTable::RemovePeer(const std::string &peer_id) {
  const auto iter = peers_.find(peer_id);
  if (iter == peers_.end()) {
    return false;
  }
  peers_.erase(iter);
  peer_activity_ms_.erase(peer_id);
  pending_candidates_.erase(peer_id);
  return true;
}

void WebrtcPeerTable::Clear() {
  peers_.clear();
  peer_activity_ms_.clear();
  pending_candidates_.clear();
}

std::vector<std::string> WebrtcPeerTable::TakePeerIdsForClient(
    const std::string &session_id, const std::string &client_id) {
  std::vector<std::string> peer_ids;
  if (session_id.empty() && client_id.empty()) {
    return peer_ids;
  }
  const bool use_client = !client_id.empty();
  const bool use_session = !use_client && !session_id.empty();
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

std::vector<std::string> WebrtcPeerTable::MarkAllClosing() {
  std::vector<std::string> peer_ids;
  for (auto &item : peers_) {
    item.second.state = WebrtcPeerState::kClosing;
    item.second.updated_at_ms = NowMs();
    peer_ids.push_back(item.first);
  }
  peer_activity_ms_.clear();
  pending_candidates_.clear();
  return peer_ids;
}

std::vector<std::string> WebrtcPeerTable::OpenPeerIds() const {
  std::vector<std::string> peer_ids;
  for (const auto &item : peers_) {
    if (IsOpenPeerState(item.second.state)) {
      peer_ids.push_back(item.first);
    }
  }
  return peer_ids;
}

uint32_t WebrtcPeerTable::ActivePeerCount() const {
  uint32_t count = 0;
  for (const auto &item : peers_) {
    if (IsOpenPeerState(item.second.state)) {
      ++count;
    }
  }
  return count;
}

bool WebrtcPeerTable::HasConnectedPeer(StreamId stream_id) const {
  for (const auto &item : peers_) {
    if (item.second.stream_id == stream_id &&
        item.second.state == WebrtcPeerState::kConnected) {
      return true;
    }
  }
  return false;
}

std::vector<WebrtcPeerInfo> WebrtcPeerTable::ConnectedPeers(
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

std::vector<WebrtcPeerInfo> WebrtcPeerTable::OpenPeers() const {
  std::vector<WebrtcPeerInfo> peers;
  peers.reserve(peers_.size());
  for (const auto &item : peers_) {
    if (IsOpenPeerState(item.second.state)) {
      peers.push_back(item.second);
    }
  }
  return peers;
}

std::vector<WebrtcPeerInfo> WebrtcPeerTable::Peers() const {
  std::vector<WebrtcPeerInfo> peers;
  peers.reserve(peers_.size());
  for (const auto &item : peers_) {
    peers.push_back(item.second);
  }
  return peers;
}

bool WebrtcPeerTable::BeginOffer(const std::string &peer_id,
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

bool WebrtcPeerTable::CompleteOffer(
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

bool WebrtcPeerTable::AddOrQueueCandidate(const WebrtcIceCandidate &candidate,
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

void WebrtcPeerTable::Touch(const std::string &peer_id) {
  const int64_t now_ms = NowMs();
  peer_activity_ms_[peer_id] = now_ms;
  auto iter = peers_.find(peer_id);
  if (iter != peers_.end()) {
    iter->second.updated_at_ms = now_ms;
  }
}

bool WebrtcPeerTable::UpdateDiagnostics(const WebrtcPeerInfo &peer) {
  auto iter = peers_.find(peer.peer_id);
  if (iter == peers_.end()) {
    return false;
  }
  iter->second.ice_selected = peer.ice_selected;
  iter->second.dtls_state = peer.dtls_state;
  iter->second.srtp_ready = peer.srtp_ready;
  iter->second.rtp_packets = peer.rtp_packets;
  iter->second.rtp_bytes = peer.rtp_bytes;
  iter->second.rtcp_packets = peer.rtcp_packets;
  iter->second.rtcp_bytes = peer.rtcp_bytes;
  iter->second.rtcp_pli_count = peer.rtcp_pli_count;
  iter->second.rtcp_fir_count = peer.rtcp_fir_count;
  iter->second.rtcp_nack_count = peer.rtcp_nack_count;
  iter->second.rtcp_transport_cc_count = peer.rtcp_transport_cc_count;
  iter->second.rtcp_keyframe_requests = peer.rtcp_keyframe_requests;
  iter->second.updated_at_ms = NowMs();
  return true;
}

bool WebrtcPeerTable::MarkClosing(const std::string &peer_id,
                                  const std::string &last_error) {
  auto iter = peers_.find(peer_id);
  if (iter == peers_.end()) {
    return false;
  }
  iter->second.state = WebrtcPeerState::kClosing;
  iter->second.last_error = last_error;
  iter->second.updated_at_ms = NowMs();
  return true;
}

EnginePeerStateUpdate WebrtcPeerTable::ApplyEngineState(
    const std::string &peer_id, WebrtcPeerState state,
    const std::string &last_error) {
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
        iter->second.last_error.clear();
        iter->second.updated_at_ms = NowMs();
        Touch(peer_id);
      }
      break;
    case WebrtcPeerState::kConnected:
      if (iter->second.state != WebrtcPeerState::kConnected) {
        iter->second.state = WebrtcPeerState::kConnected;
        iter->second.last_error.clear();
        iter->second.updated_at_ms = NowMs();
        update.stream_id = iter->second.stream_id;
        update.request_key_frame = true;
      }
      Touch(peer_id);
      break;
    case WebrtcPeerState::kFailed:
      iter->second.state = WebrtcPeerState::kFailed;
      iter->second.last_error = last_error;
      iter->second.updated_at_ms = NowMs();
      peer_activity_ms_.erase(peer_id);
      pending_candidates_.erase(peer_id);
      break;
    case WebrtcPeerState::kClosed:
    case WebrtcPeerState::kClosing:
      iter->second.state = WebrtcPeerState::kClosed;
      if (!last_error.empty()) {
        iter->second.last_error = last_error;
      }
      iter->second.updated_at_ms = NowMs();
      peer_activity_ms_.erase(peer_id);
      pending_candidates_.erase(peer_id);
      break;
    case WebrtcPeerState::kCreated:
    case WebrtcPeerState::kOfferReceived:
      break;
  }
  return update;
}

bool WebrtcPeerTable::GetOpenPeerStream(const std::string &peer_id,
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

std::vector<std::string> WebrtcPeerTable::FindStaleSetupPeerIds(
    int64_t timeout_ms) {
  std::vector<std::string> peer_ids;
  const int64_t now_ms = NowMs();
  for (auto iter = peers_.begin(); iter != peers_.end(); ++iter) {
    auto activity_iter = peer_activity_ms_.find(iter->first);
    if (activity_iter == peer_activity_ms_.end()) {
      activity_iter =
          peer_activity_ms_.insert(std::make_pair(iter->first, now_ms)).first;
    }
    if (IsSetupPeerState(iter->second.state) &&
        now_ms - activity_iter->second >= timeout_ms) {
      peer_ids.push_back(iter->first);
    }
  }
  return peer_ids;
}

bool WebrtcPeerTable::IsOpenPeerState(WebrtcPeerState state) {
  return state != WebrtcPeerState::kClosing &&
         state != WebrtcPeerState::kClosed &&
         state != WebrtcPeerState::kFailed;
}

bool WebrtcPeerTable::IsSetupPeerState(WebrtcPeerState state) {
  return state == WebrtcPeerState::kCreated ||
         state == WebrtcPeerState::kOfferReceived ||
         state == WebrtcPeerState::kConnecting;
}

int64_t WebrtcPeerTable::NowMs() { return infra::Time::MonotonicMillis(); }

std::string WebrtcPeerTable::NextPeerId() {
  std::string id = "webrtc-";
  id += std::to_string(next_peer_id_++);
  return id;
}

}  // namespace webrtc_internal
}  // namespace live_stream
