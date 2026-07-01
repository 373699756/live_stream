#ifndef LIVE_STREAM_RTSP_SRC_RTSP_REQUEST_HANDLER_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_REQUEST_HANDLER_H_

#include "media/media_streams.h"
#include "rtsp_protocol.h"
#include "rtsp_session.h"

#include <map>
#include <memory>
#include <string>

namespace live_stream {

class IRtspRequestHandlerDelegate {
public:
    virtual ~IRtspRequestHandlerDelegate() = default;

    // Delegate 负责所有跨模块动作：媒体状态、认证、transport、subscription 和网络发送。
    // RtspRequestHandler 只解析 RTSP 方法并维护协议状态码。
    virtual bool IsRtspStreamAvailable(StreamId stream_id) const = 0;
    virtual MediaStreamInfo RtspStreamInfoForStream(StreamId stream_id) const = 0;
    virtual bool AuthorizeRtspRequest(RtspSession &session,
                                      const rtsp_internal::RtspRequest &request,
                                      StreamId stream_id) = 0;
    virtual bool SetupRtspTransport(RtspSession &session,
                                    const rtsp_internal::RtspRequest &request,
                                    StreamId stream_id) = 0;
    virtual int StartRtspMediaStream(RtspSession &session) = 0;
    virtual void StartRtspMediaSend(
        const std::shared_ptr<RtspSession> &session) = 0;
    virtual void CloseRtspConnectionAfterSend(ConnectionId connection_id) = 0;
    virtual void SendRtspResponse(
        ConnectionId connection_id, int status, const std::string &cseq,
        const std::map<std::string, std::string> &headers,
        const std::string &body) = 0;
    virtual RtspListenAddress RtspLocalAddress() const = 0;
};

class RtspRequestHandler {
public:
    explicit RtspRequestHandler(IRtspRequestHandlerDelegate &delegate);

    void HandleRequest(const std::shared_ptr<RtspSession> &session,
                       const rtsp_internal::RtspRequest &request);

private:
    // SendResponse 保留 CSeq，符合 RTSP 客户端用 CSeq 匹配异步响应的习惯。
    void SendResponse(ConnectionId connection_id, int status,
                      const rtsp_internal::RtspRequest &request,
                      const std::map<std::string, std::string> &headers,
                      const std::string &body);
    void HandleDescribe(RtspSession &session,
                        const rtsp_internal::RtspRequest &request,
                        StreamId stream_id);
    void HandlePlay(const std::shared_ptr<RtspSession> &session,
                    const rtsp_internal::RtspRequest &request);
    void HandleTeardown(RtspSession &session,
                        const rtsp_internal::RtspRequest &request);

    IRtspRequestHandlerDelegate &delegate_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_REQUEST_HANDLER_H_
