#ifndef LIVE_STREAM_RTSP_SRC_RTSP_REQUEST_HANDLER_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_REQUEST_HANDLER_H_

#include "rtsp_protocol.h"
#include "rtsp_session.h"

#include <map>
#include <memory>
#include <string>

namespace live_stream {

class IRtspRequestHandlerDelegate {
 public:
  virtual ~IRtspRequestHandlerDelegate() = default;

  virtual bool IsRtspStreamAvailable(StreamId stream_id) const = 0;
  virtual MediaTrack RtspTrackForStream(StreamId stream_id) const = 0;
  virtual bool AuthorizeRtspRequest(const std::shared_ptr<RtspSession> &session,
                                    const rtsp_internal::RtspRequest &request,
                                    StreamId stream_id) = 0;
  virtual bool SetupRtspTransport(const std::shared_ptr<RtspSession> &session,
                                  const rtsp_internal::RtspRequest &request,
                                  StreamId stream_id) = 0;
  virtual bool StartRtspPlayback(const std::shared_ptr<RtspSession> &session) = 0;
  virtual void ArmRtspPlayback(const std::shared_ptr<RtspSession> &session) = 0;
  virtual void CloseRtspConnectionAfterSend(ConnectionId connection_id) = 0;
  virtual void SendRtspResponse(
      ConnectionId connection_id, int status, const std::string &cseq,
      const std::map<std::string, std::string> &headers,
      const std::string &body) = 0;
  virtual RtspListenAddress RtspLocalAddress() const = 0;
};

class RtspRequestHandler {
 public:
  explicit RtspRequestHandler(IRtspRequestHandlerDelegate *delegate);

  void HandleRequest(const std::shared_ptr<RtspSession> &session,
                     const rtsp_internal::RtspRequest &request);

 private:
  void SendResponse(ConnectionId connection_id, int status,
                    const rtsp_internal::RtspRequest &request,
                    const std::map<std::string, std::string> &headers,
                    const std::string &body);
  void HandleDescribe(const std::shared_ptr<RtspSession> &session,
                      const rtsp_internal::RtspRequest &request,
                      StreamId stream_id);
  void HandlePlay(const std::shared_ptr<RtspSession> &session,
                  const rtsp_internal::RtspRequest &request);
  void HandleTeardown(const std::shared_ptr<RtspSession> &session,
                      const rtsp_internal::RtspRequest &request);

  IRtspRequestHandlerDelegate *delegate_ = nullptr;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_REQUEST_HANDLER_H_
