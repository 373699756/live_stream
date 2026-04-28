#ifndef LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_ENGINE_H_
#define LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_ENGINE_H_

#include "webrtc_service.h"

#include <memory>
#include <string>

namespace live_stream {
namespace webrtc_internal {

class IWebrtcEngine {
 public:
    virtual ~IWebrtcEngine() = default;

    virtual const char* Name() const = 0;
    virtual bool Available() const = 0;
    virtual infra::Status Init(const WebrtcServiceOptions& options) = 0;
    virtual void Deinit() = 0;
    virtual infra::Status CreatePeer(const WebrtcPeerInfo& peer) = 0;
    virtual infra::Result<std::string> HandleOffer(
        const WebrtcPeerInfo& peer,
        const std::string& offer_sdp) = 0;
    virtual infra::Status AddIceCandidate(
        const WebrtcIceCandidate& candidate) = 0;
    virtual infra::Status ClosePeer(const std::string& peer_id) = 0;
    virtual infra::Status SendFrame(const WebrtcPeerInfo& peer,
                                    const infra::EncodedFrame& frame) = 0;
};

std::unique_ptr<IWebrtcEngine> CreateEngine(bool use_fake_engine);

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SERVICE_SRC_WEBRTC_ENGINE_H_
