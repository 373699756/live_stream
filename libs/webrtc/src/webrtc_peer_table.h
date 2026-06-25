#ifndef LIVE_STREAM_WEBRTC_SRC_WEBRTC_PEER_TABLE_H_
#define LIVE_STREAM_WEBRTC_SRC_WEBRTC_PEER_TABLE_H_

#include "webrtc.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace live_stream {
namespace webrtc_internal {

struct PeerHostStateUpdate {
    bool found = false;
    bool need_keyframe = false;
    StreamId stream_id = StreamId::kMain;
};

// WebrtcPeerTable owns signaling peer state and pending ICE candidates. It is
// intentionally not internally synchronized; WebrtcImpl protects it.
class WebrtcPeerTable {
public:
    WebrtcPeerInfo CreatePeer(const WebrtcCreatePeerRequest &request,
                              Codec codec);
    bool Contains(const std::string &peer_id) const;
    WebrtcPeerInfo GetPeer(const std::string &peer_id) const;
    bool RemovePeer(const std::string &peer_id);
    void Clear();

    std::vector<std::string> TakePeerIdsForClient(
        const std::string &session_id, const std::string &client_id);
    std::vector<std::string> MarkAllClosing();
    std::vector<std::string> OpenPeerIds() const;
    uint32_t ActivePeers() const;
    bool IsStreamConnected(StreamId stream_id) const;
    std::vector<WebrtcPeerInfo> ConnectedPeers(StreamId stream_id) const;
    std::vector<WebrtcPeerInfo> OpenPeers() const;
    std::vector<WebrtcPeerInfo> Peers() const;

    bool BeginOffer(const std::string &peer_id, WebrtcPeerInfo &peer);
    bool CompleteOffer(const std::string &peer_id, WebrtcPeerInfo &peer,
                       std::vector<WebrtcIceCandidate> &pending_candidates);
    bool AddOrQueueCandidate(const WebrtcIceCandidate &candidate, bool &queued);
    void Touch(const std::string &peer_id);
    bool UpdatePeerInfo(const WebrtcPeerInfo &peer);
    bool MarkClosing(const std::string &peer_id,
                     const std::string &last_error);

    PeerHostStateUpdate ApplyPeerHostState(const std::string &peer_id,
                                           WebrtcPeerState state,
                                           const std::string &last_error);
    bool GetOpenPeerStream(const std::string &peer_id,
                           StreamId &stream_id) const;
    std::vector<std::string> FindStaleSetupPeerIds(int64_t timeout_ms);

private:
    static bool IsOpenPeerState(WebrtcPeerState state);
    static bool IsSetupPeerState(WebrtcPeerState state);
    static int64_t NowMs();
    std::string NextPeerId();

    std::map<std::string, WebrtcPeerInfo> peers_;
    std::map<std::string, int64_t> peer_activity_ms_;
    std::map<std::string, std::vector<WebrtcIceCandidate>> pending_candidates_;
    uint64_t next_peer_id_ = 1;
};

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_WEBRTC_PEER_TABLE_H_
