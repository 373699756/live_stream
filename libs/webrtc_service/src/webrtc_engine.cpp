#include "webrtc_engine.h"

#include "webrtc_sdp.h"

#include <map>

namespace live_stream {
namespace webrtc_internal {
namespace {

class MetaRtcEngine : public IWebrtcEngine {
 public:
    const char* Name() const override { return "metaRTC"; }
    bool Available() const override { return true; }

    bool Start(const WebrtcServiceOptions& options) override {
        (void)options;
        return true;
    }

    void Stop() override {}

    bool CreatePeer(const WebrtcPeerInfo& peer) override {
        peers_[peer.peer_id] = peer;
        return true;
    }

    std::string HandleOffer(
        const WebrtcPeerInfo& peer,
        const std::string& offer_sdp) override {
        if (peers_.find(peer.peer_id) == peers_.end() || offer_sdp.empty()) {
            return std::string();
        }
        return BuildAnswerSdp();
    }

    bool AddIceCandidate(const WebrtcIceCandidate& candidate) override {
        last_candidate_json_ = BuildCandidateJson(candidate);
        return peers_.find(candidate.peer_id) != peers_.end();
    }

    bool ClosePeer(const std::string& peer_id) override {
        peers_.erase(peer_id);
        return true;
    }

    bool SendFrame(const WebrtcPeerInfo& peer,
                   const EncodedFrame& frame) override {
        if (peers_.find(peer.peer_id) == peers_.end() || !frame.buffer ||
            frame.size == 0) {
            return false;
        }
        ++sent_frames_;
        return true;
    }

 private:
    std::string BuildAnswerSdp() const {
        return "v=0\r\n"
               "o=- 0 0 IN IP4 127.0.0.1\r\n"
               "s=live_stream\r\n"
               "t=0 0\r\n"
               "a=group:BUNDLE 0\r\n"
               "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
               "c=IN IP4 0.0.0.0\r\n"
               "a=mid:0\r\n"
               "a=sendonly\r\n"
               "a=rtcp-mux\r\n"
               "a=rtpmap:96 H264/90000\r\n";
    }

    std::map<std::string, WebrtcPeerInfo> peers_;
    std::string last_candidate_json_;
    uint64_t sent_frames_ = 0;
};

class FakeWebrtcEngine : public IWebrtcEngine {
 public:
    const char* Name() const override { return "fake_webrtc"; }
    bool Available() const override { return true; }

    bool Start(const WebrtcServiceOptions& options) override {
        (void)options;
        return true;
    }

    void Stop() override { peers_.clear(); }

    bool CreatePeer(const WebrtcPeerInfo& peer) override {
        peers_[peer.peer_id] = peer;
        return true;
    }

    std::string HandleOffer(
        const WebrtcPeerInfo& peer,
        const std::string& offer_sdp) override {
        if (peers_.find(peer.peer_id) == peers_.end() || offer_sdp.empty()) {
            return std::string();
        }
        return "v=0\r\ns=fake-webrtc-answer\r\n";
    }

    bool AddIceCandidate(const WebrtcIceCandidate& candidate) override {
        last_candidate_json_ = BuildCandidateJson(candidate);
        return peers_.find(candidate.peer_id) != peers_.end();
    }

    bool ClosePeer(const std::string& peer_id) override {
        peers_.erase(peer_id);
        return true;
    }

    bool SendFrame(const WebrtcPeerInfo& peer,
                   const EncodedFrame& frame) override {
        if (peers_.find(peer.peer_id) == peers_.end() || !frame.buffer ||
            frame.size == 0) {
            return false;
        }
        ++sent_frames_;
        return true;
    }

 private:
    std::map<std::string, WebrtcPeerInfo> peers_;
    std::string last_candidate_json_;
    uint64_t sent_frames_ = 0;
};

}  // namespace

std::unique_ptr<IWebrtcEngine> CreateEngine(bool use_fake_engine) {
    if (use_fake_engine) {
        return std::unique_ptr<IWebrtcEngine>(new FakeWebrtcEngine());
    }
    return std::unique_ptr<IWebrtcEngine>(new MetaRtcEngine());
}

}  // namespace webrtc_internal
}  // namespace live_stream
