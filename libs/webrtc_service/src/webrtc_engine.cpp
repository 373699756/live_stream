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

    infra::Status Init(const WebrtcServiceOptions& options) override {
        (void)options;
        return infra::Status::kOk;
    }

    void Deinit() override {}

    infra::Status CreatePeer(const WebrtcPeerInfo& peer) override {
        peers_[peer.peer_id] = peer;
        return infra::Status::kOk;
    }

    infra::Result<std::string> HandleOffer(
        const WebrtcPeerInfo& peer,
        const std::string& offer_sdp) override {
        if (peers_.find(peer.peer_id) == peers_.end() || offer_sdp.empty()) {
            return infra::Result<std::string>::Fail(infra::Status::kNotFound);
        }
        return infra::Result<std::string>::Ok(BuildAnswerSdp());
    }

    infra::Status AddIceCandidate(
        const WebrtcIceCandidate& candidate) override {
        last_candidate_json_ = BuildCandidateJson(candidate);
        return peers_.find(candidate.peer_id) == peers_.end()
                   ? infra::Status::kNotFound
                   : infra::Status::kOk;
    }

    infra::Status ClosePeer(const std::string& peer_id) override {
        peers_.erase(peer_id);
        return infra::Status::kOk;
    }

    infra::Status SendFrame(const WebrtcPeerInfo& peer,
                            const infra::EncodedFrame& frame) override {
        if (peers_.find(peer.peer_id) == peers_.end() || !frame.buffer ||
            frame.size == 0) {
            return infra::Status::kInvalidParam;
        }
        ++sent_frames_;
        return infra::Status::kOk;
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

    infra::Status Init(const WebrtcServiceOptions& options) override {
        (void)options;
        return infra::Status::kOk;
    }

    void Deinit() override { peers_.clear(); }

    infra::Status CreatePeer(const WebrtcPeerInfo& peer) override {
        peers_[peer.peer_id] = peer;
        return infra::Status::kOk;
    }

    infra::Result<std::string> HandleOffer(
        const WebrtcPeerInfo& peer,
        const std::string& offer_sdp) override {
        if (peers_.find(peer.peer_id) == peers_.end() || offer_sdp.empty()) {
            return infra::Result<std::string>::Fail(infra::Status::kNotFound);
        }
        return infra::Result<std::string>::Ok(
            "v=0\r\ns=fake-webrtc-answer\r\n");
    }

    infra::Status AddIceCandidate(
        const WebrtcIceCandidate& candidate) override {
        last_candidate_json_ = BuildCandidateJson(candidate);
        return peers_.find(candidate.peer_id) == peers_.end()
                   ? infra::Status::kNotFound
                   : infra::Status::kOk;
    }

    infra::Status ClosePeer(const std::string& peer_id) override {
        peers_.erase(peer_id);
        return infra::Status::kOk;
    }

    infra::Status SendFrame(const WebrtcPeerInfo& peer,
                            const infra::EncodedFrame& frame) override {
        if (peers_.find(peer.peer_id) == peers_.end() || !frame.buffer ||
            frame.size == 0) {
            return infra::Status::kInvalidParam;
        }
        ++sent_frames_;
        return infra::Status::kOk;
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
