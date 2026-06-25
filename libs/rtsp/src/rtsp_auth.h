#ifndef LIVE_STREAM_RTSP_SRC_RTSP_AUTH_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_AUTH_H_

#include "auth.h"
#include "rtsp_protocol.h"
#include "rtsp_session.h"

#include <map>
#include <string>

namespace live_stream {
namespace rtsp_internal {

class IRtspAuthResponder {
public:
    virtual ~IRtspAuthResponder() = default;

    virtual void AddAuthFailure() = 0;
    virtual void SendAuthResponse(
        ConnectionId connection_id, int status, const std::string &cseq,
        const std::map<std::string, std::string> &headers,
        const std::string &body) = 0;
};

class RtspAuth {
public:
    RtspAuth(IAuth *auth, IRtspAuthResponder &responder);

    bool Authorize(RtspSession &session, const RtspRequest &request,
                   StreamId stream_id);
    void Clear();

private:
    void SendChallenge(ConnectionId connection_id, const std::string &cseq);

    IAuth *auth_ = nullptr;
    IRtspAuthResponder &responder_;
};

}  // namespace rtsp_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_AUTH_H_
