#ifndef LIVE_STREAM_WEBRTC_SRC_WEBRTC_ENGINE_H_
#define LIVE_STREAM_WEBRTC_SRC_WEBRTC_ENGINE_H_

#include "webrtc.h"

#include <memory>
#include <string>

namespace live_stream {
namespace webrtc_internal {

struct WebrtcEngineCallbacks {
    void *user = nullptr;
    void (*OnPeerStateChanged)(void *user, const char *peer_id,
                               WebrtcPeerState state) = nullptr;
    void (*OnPeerKeyFrameRequested)(void *user, const char *peer_id) = nullptr;
};

class IWebrtcEngine {
public:
    virtual ~IWebrtcEngine() = default;

    virtual bool Available() const = 0;
    virtual bool Start(const WebrtcOptions &options,
                       const WebrtcEngineCallbacks &callbacks) = 0;
    virtual void Stop() = 0;
    virtual bool CreatePeer(const WebrtcPeerInfo &peer) = 0;
    virtual std::string HandleOffer(const WebrtcPeerInfo &peer,
                                    const std::string &offer_sdp) = 0;
    virtual bool AddIceCandidate(const WebrtcIceCandidate &candidate) = 0;
    virtual bool ClosePeer(const std::string &peer_id) = 0;
    virtual bool SendFrame(const WebrtcPeerInfo &peer,
                           const FramePayload &frame) = 0;
};

std::unique_ptr<IWebrtcEngine> CreateEngine(bool use_fake_engine);

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_WEBRTC_ENGINE_H_
