#include "rtsp_auth.h"

#include "infra/log.h"
#include "infra/time.h"

namespace live_stream {
namespace rtsp_internal {
namespace {

constexpr int64_t kRtspPeerAuthTtlMs = 10000;

}  // namespace

RtspAuth::RtspAuth(IAuth *auth, IRtspAuthResponder *responder)
    : auth_(auth), responder_(responder) {}

bool RtspAuth::Authorize(const std::shared_ptr<RtspSession> &session,
                         const RtspRequest &request,
                         StreamId stream_id) {
    if (session == nullptr || responder_ == nullptr) {
        return false;
    }
    if (auth_ == nullptr) {
        Error("rtsp", "RTSP auth service unavailable");
        responder_->SendAuthResponse(session->connection_id, 500,
                                     CSeq(request), {}, "");
        return false;
    }
    if (session->IsAuthenticatedFor(stream_id)) {
        return true;
    }
    const std::string authorization = HeaderValue(request, "Authorization");
    const std::string prefix = "Basic ";
    if (authorization.compare(0, prefix.size(), prefix) != 0) {
        std::string cached_user_name;
        if (FindPeerGrant(session->peer.ip, stream_id, &cached_user_name)) {
            session->MarkAuthenticated(stream_id, cached_user_name);
            Info("rtsp", "RTSP auth reused peer=%s user=%s uri=%s",
                 session->peer.ip.c_str(), cached_user_name.c_str(),
                 request.uri.c_str());
            return true;
        }
        responder_->AddAuthFailure();
        Info("rtsp", "RTSP auth required peer=%s uri=%s",
             session->peer.ip.c_str(), request.uri.c_str());
        SendChallenge(session->connection_id, CSeq(request));
        return false;
    }

    std::string decoded;
    if (!DecodeBase64(authorization.substr(prefix.size()), &decoded)) {
        responder_->AddAuthFailure();
        Error("rtsp", "RTSP auth invalid base64 peer=%s",
              session->peer.ip.c_str());
        SendChallenge(session->connection_id, CSeq(request));
        return false;
    }
    const size_t colon = decoded.find(':');
    if (colon == std::string::npos) {
        responder_->AddAuthFailure();
        Error("rtsp", "RTSP auth invalid credential peer=%s",
              session->peer.ip.c_str());
        SendChallenge(session->connection_id, CSeq(request));
        return false;
    }

    LoginRequest login;
    login.context.client_ip = session->peer.ip;
    login.user_name = decoded.substr(0, colon);
    login.password = decoded.substr(colon + 1);
    LoginResult login_result = auth_->Login(login);
    if (login_result.token.empty()) {
        responder_->AddAuthFailure();
        Error("rtsp", "RTSP auth rejected peer=%s user=%s",
              session->peer.ip.c_str(), login.user_name.c_str());
        SendChallenge(session->connection_id, CSeq(request));
        return false;
    }

    live_stream::RequestContext logout_context;
    logout_context.user_name = login_result.principal.user_name;
    logout_context.session_id = login_result.principal.session_id;
    if (login_result.must_change_password) {
        static_cast<void>(auth_->Logout(logout_context));
        responder_->AddAuthFailure();
        Error("rtsp",
              "RTSP auth rejected peer=%s user=%s "
              "reason=must_change_password",
              session->peer.ip.c_str(), login.user_name.c_str());
        responder_->SendAuthResponse(session->connection_id, 403,
                                     CSeq(request), {}, "");
        return false;
    }

    const std::string target = StreamPath(stream_id);
    if (!auth_->CheckPermission(login_result.principal,
                                AuthPermission::kPreviewVideo, target)) {
        static_cast<void>(auth_->Logout(logout_context));
        responder_->AddAuthFailure();
        Error("rtsp", "RTSP auth forbidden peer=%s user=%s target=%s",
              session->peer.ip.c_str(), login.user_name.c_str(),
              target.c_str());
        responder_->SendAuthResponse(session->connection_id, 403,
                                     CSeq(request), {}, "");
        return false;
    }
    static_cast<void>(auth_->Logout(logout_context));
    session->MarkAuthenticated(stream_id, login_result.principal.user_name);
    RememberPeerGrant(session->peer.ip, stream_id,
                      login_result.principal.user_name);
    Info("rtsp", "RTSP auth accepted peer=%s user=%s target=%s",
         session->peer.ip.c_str(), login_result.principal.user_name.c_str(),
         target.c_str());
    return true;
}

void RtspAuth::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    peer_auth_grants_.clear();
}

bool RtspAuth::FindPeerGrant(const std::string &peer_ip,
                             StreamId stream_id,
                             std::string *user_name) {
    const int64_t now_ms = infra::Time::MonotonicMillis();
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = peer_auth_grants_.begin(); it != peer_auth_grants_.end();) {
        if (it->expires_at_ms <= now_ms) {
            it = peer_auth_grants_.erase(it);
            continue;
        }
        if (it->peer_ip == peer_ip && it->stream_id == stream_id) {
            if (user_name != nullptr) {
                *user_name = it->user_name;
            }
            it->expires_at_ms = now_ms + kRtspPeerAuthTtlMs;
            return true;
        }
        ++it;
    }
    return false;
}

void RtspAuth::RememberPeerGrant(const std::string &peer_ip,
                                 StreamId stream_id,
                                 const std::string &user_name) {
    if (peer_ip.empty() || user_name.empty()) {
        return;
    }
    const int64_t expires_at_ms =
        infra::Time::MonotonicMillis() + kRtspPeerAuthTtlMs;
    std::lock_guard<std::mutex> lock(mutex_);
    for (PeerAuthGrant &grant : peer_auth_grants_) {
        if (grant.peer_ip == peer_ip && grant.stream_id == stream_id) {
            grant.user_name = user_name;
            grant.expires_at_ms = expires_at_ms;
            return;
        }
    }
    PeerAuthGrant grant;
    grant.peer_ip = peer_ip;
    grant.stream_id = stream_id;
    grant.user_name = user_name;
    grant.expires_at_ms = expires_at_ms;
    peer_auth_grants_.push_back(grant);
}

void RtspAuth::SendChallenge(ConnectionId connection_id,
                             const std::string &cseq) {
    responder_->SendAuthResponse(
        connection_id, 401, cseq,
        {{"WWW-Authenticate", BasicRealmHeader()}}, "");
}

}  // namespace rtsp_internal
}  // namespace live_stream
