#ifndef LIVE_STREAM_WEBRTC_SRC_WEBRTC_ENGINE_H_
#define LIVE_STREAM_WEBRTC_SRC_WEBRTC_ENGINE_H_

#include "webrtc.h"

#include "rtp.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class INetEngine;
class INetExecutor;

namespace webrtc_internal {

struct WebrtcEngineCallbacks {
    void *user = nullptr;
    void (*OnPeerStateChanged)(void *user, const char *peer_id,
                               WebrtcPeerState state,
                               const char *last_error) = nullptr;
    void (*OnPeerKeyFrameRequested)(void *user, const char *peer_id) = nullptr;
};

struct WebrtcRtpSendParameters {
    VideoCodec codec = VideoCodec::kH264;
    uint8_t payload_type = 0;
    uint32_t clock_rate = rtp::kRtpClockRate;
    uint32_t ssrc = 0;
};

class IWebrtcEngine {
public:
    virtual ~IWebrtcEngine() = default;

    virtual bool Available() const = 0;
    virtual bool Start(const WebrtcOptions &options,
                       const WebrtcEngineCallbacks &callbacks) = 0;
    virtual void Stop() = 0;
    virtual bool ApplyOptions(const WebrtcOptions &options) = 0;
    virtual bool CreatePeer(const WebrtcPeerInfo &peer) = 0;
    virtual std::string HandleOffer(const WebrtcPeerInfo &peer,
                                    const std::string &offer_sdp) = 0;
    virtual bool AddIceCandidate(const WebrtcIceCandidate &candidate) = 0;
    virtual bool ClosePeer(const std::string &peer_id) = 0;
    virtual bool HandleDtlsPacket(const std::string &peer_id,
                                  const uint8_t *data, size_t size,
                                  std::vector<uint8_t> *outgoing_dtls) = 0;
    virtual bool HandleSrtcpPacket(const std::string &peer_id,
                                   const uint8_t *data, size_t size) = 0;
    virtual bool SendRtpPacket(const WebrtcPeerInfo &peer,
                               const EncodedFrame &frame,
                               const rtp::RtpPacketView &packet) = 0;
    virtual bool GetRtpSendParameters(
        const std::string &peer_id,
        WebrtcRtpSendParameters *parameters) const = 0;
    virtual bool FillPeerDiagnostics(const std::string &peer_id,
                                     WebrtcPeerInfo *peer) const = 0;
    virtual void FillStats(WebrtcStats *stats) const = 0;
};

std::unique_ptr<IWebrtcEngine> CreateWebrtcEngine(
    INetEngine *net_engine,
    INetExecutor *net_executor);

}  // namespace webrtc_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_WEBRTC_SRC_WEBRTC_ENGINE_H_
