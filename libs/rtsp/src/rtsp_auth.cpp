#include "rtsp_auth.h"

#include "infra/log.h"

namespace live_stream {
namespace rtsp_internal {

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
    // RTSP Basic auth 只在本 session 内缓存“已授权的 stream”，不同码流必须重新校验，
    // 避免拿 main 的权限直接播放 sub 或反过来。
    if (session->IsAuthenticatedFor(stream_id)) {
        return true;
    }
    const std::string authorization = HeaderValue(request, "Authorization");
    const std::string prefix = "Basic ";
    if (authorization.compare(0, prefix.size(), prefix) != 0) {
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
    // 复用 auth 登录/权限逻辑做校验，但 RTSP 不持有 Web session token；
    // 校验结束立即 Logout，只把本 RTSP session 标记为已授权。
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
    Info("rtsp", "RTSP auth accepted peer=%s user=%s target=%s",
         session->peer.ip.c_str(), login_result.principal.user_name.c_str(),
         target.c_str());
    return true;
}

void RtspAuth::Clear() {}

void RtspAuth::SendChallenge(ConnectionId connection_id,
                             const std::string &cseq) {
    responder_->SendAuthResponse(
        connection_id, 401, cseq,
        {{"WWW-Authenticate", BasicRealmHeader()}}, "");
}

}  // namespace rtsp_internal
}  // namespace live_stream
