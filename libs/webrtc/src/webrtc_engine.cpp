#include "webrtc_engine.h"

#include <map>
#include <mutex>
#include <string>

namespace live_stream {
namespace webrtc_internal {
namespace {

bool IsSupportedCodec(VideoCodec codec) {
    return codec == VideoCodec::kH264 || codec == VideoCodec::kH265;
}

class NativeWebrtcEngine : public IWebrtcEngine {
public:
    bool Available() const override { return true; }

    bool Start(const WebrtcOptions &options,
               const WebrtcEngineCallbacks &callbacks) override {
        std::lock_guard<std::mutex> guard(mutex_);
        options_ = options;
        callbacks_ = callbacks;
        peers_.clear();
        return true;
    }

    void Stop() override {
        std::lock_guard<std::mutex> guard(mutex_);
        peers_.clear();
    }

    bool CreatePeer(const WebrtcPeerInfo &peer) override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (peer.peer_id.empty() || !IsSupportedCodec(peer.codec) ||
            peers_.find(peer.peer_id) != peers_.end()) {
            return false;
        }
        peers_[peer.peer_id] = PeerRuntime{peer, std::string(), false};
        return true;
    }

    std::string HandleOffer(const WebrtcPeerInfo &peer,
                            const std::string &offer_sdp) override {
        WebrtcEngineCallbacks callbacks;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer.peer_id);
            if (it == peers_.end() || offer_sdp.empty()) {
                return std::string();
            }
            it->second.offer_sdp = offer_sdp;
            callbacks = callbacks_;
        }
        if (callbacks.OnPeerStateChanged != nullptr) {
            callbacks.OnPeerStateChanged(callbacks.user, peer.peer_id.c_str(),
                                         WebrtcPeerState::kConnecting);
        }
        return std::string();
    }

    bool AddIceCandidate(const WebrtcIceCandidate &candidate) override {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = peers_.find(candidate.peer_id);
        if (it == peers_.end() || candidate.candidate.empty()) {
            return false;
        }
        it->second.has_remote_candidate = true;
        return true;
    }

    bool ClosePeer(const std::string &peer_id) override {
        WebrtcEngineCallbacks callbacks;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            auto it = peers_.find(peer_id);
            if (it == peers_.end()) {
                return false;
            }
            peers_.erase(it);
            callbacks = callbacks_;
        }
        if (callbacks.OnPeerStateChanged != nullptr) {
            callbacks.OnPeerStateChanged(callbacks.user, peer_id.c_str(),
                                         WebrtcPeerState::kClosed);
        }
        return true;
    }

    bool SendFrame(const WebrtcPeerInfo &peer,
                   const FramePayload &frame) override {
        const EncodedFrame &encoded_frame = frame.encoded_frame;
        std::lock_guard<std::mutex> guard(mutex_);
        return peers_.find(peer.peer_id) != peers_.end() &&
               EncodedFrameHasPayload(&encoded_frame) && frame.has_nal_units;
    }

private:
    struct PeerRuntime {
        WebrtcPeerInfo peer;
        std::string offer_sdp;
        bool has_remote_candidate = false;
    };

    mutable std::mutex mutex_;
    WebrtcEngineCallbacks callbacks_;
    WebrtcOptions options_;
    std::map<std::string, PeerRuntime> peers_;
};

}  // namespace

std::unique_ptr<IWebrtcEngine> CreateEngine(bool use_fake_engine) {
    (void)use_fake_engine;
    return std::unique_ptr<IWebrtcEngine>(new NativeWebrtcEngine());
}

}  // namespace webrtc_internal
}  // namespace live_stream
