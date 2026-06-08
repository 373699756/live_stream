#ifndef LIVE_STREAM_RTSP_SRC_RTSP_AUTH_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_AUTH_H_

#include "auth.h"
#include "rtsp_protocol.h"
#include "rtsp_session.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

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
    RtspAuth(IAuth *auth, IRtspAuthResponder *responder);

    bool Authorize(const std::shared_ptr<RtspSession> &session,
                   const RtspRequest &request,
                   StreamId stream_id);
    void Clear();

private:
    struct PeerAuthGrant {
        std::string peer_ip;
        StreamId stream_id = StreamId::kMain;
        std::string user_name;
        int64_t expires_at_ms = 0;
    };

    bool FindPeerGrant(const std::string &peer_ip,
                       StreamId stream_id,
                       std::string *user_name);
    void RememberPeerGrant(const std::string &peer_ip,
                           StreamId stream_id,
                           const std::string &user_name);
    void SendChallenge(ConnectionId connection_id, const std::string &cseq);

    IAuth *auth_ = nullptr;
    IRtspAuthResponder *responder_ = nullptr;
    std::mutex mutex_;
    std::vector<PeerAuthGrant> peer_auth_grants_;
};

}  // namespace rtsp_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_AUTH_H_
