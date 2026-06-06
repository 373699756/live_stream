#ifndef LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_

#include "media/encoded_frame.h"
#include "net.h"
#include "rtsp.h"
#include "rtsp_session.h"
#include "media_mux.h"

#include <memory>
#include <mutex>

namespace live_stream {

class RtspRtpPacketSink;

struct RtspRtpSenderContext {
  NetEngine *net_engine = nullptr;
  std::mutex *mutex = nullptr;
  RtspStats *service_stats = nullptr;
  IRtspAdaptiveObserver *adaptive_observer = nullptr;
};

class RtspRtpSender {
 public:
  explicit RtspRtpSender(uint32_t rtp_mtu_bytes);

  void SendFrame(const std::shared_ptr<RtspSession> &session,
                 const EncodedFrame &frame,
                 const RtspRtpSenderContext &context);

 private:
  friend class RtspRtpPacketSink;

  bool SendRtpPacketView(const std::shared_ptr<RtspSession> &session,
                         const EncodedFrame &frame,
                         const media_mux::RtpPacketView &packet,
                         const RtspRtpSenderContext &context);
  void NotifyAdaptive(const RtspRtpSenderContext &context,
                      const RtspSession &session,
                      RtspAdaptiveEventType event) const;

  media_mux::RtpPacketizer packetizer_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_RTP_SENDER_H_
