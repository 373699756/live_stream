#include "rtsp_request_handler.h"

#include "infra/log.h"
#include "rtsp_muxer.h"

namespace live_stream {
namespace {

constexpr const char *kRtspRequestHandlerModule = "rtsp";

std::string BuildRtpInfo(const std::shared_ptr<RtspSession> &session,
                         const rtsp_internal::RtspRequest &request) {
    const std::string control_url =
        request.uri.empty()
            ? std::string(rtsp_internal::StreamPath(session->stream_id))
            : request.uri;
    return "url=" + control_url +
           ";seq=" + std::to_string(session->rtp_sequence) +
           ";rtptime=" + std::to_string(session->play_rtp_timestamp);
}

}  // namespace

using rtsp_internal::CSeq;
using rtsp_internal::PathToStreamId;
using rtsp_internal::StreamPath;

RtspRequestHandler::RtspRequestHandler(
    IRtspRequestHandlerDelegate *delegate)
    : delegate_(delegate) {}

void RtspRequestHandler::HandleRequest(
    const std::shared_ptr<RtspSession> &session,
    const rtsp_internal::RtspRequest &request) {
    if (session == nullptr || delegate_ == nullptr) {
        return;
    }
    Info(kRtspRequestHandlerModule,
         "RTSP request conn=%llu peer=%s:%u method=%s uri=%s",
         static_cast<unsigned long long>(session->connection_id),
         session->peer.ip.c_str(),
         static_cast<unsigned>(session->peer.port),
         request.method.c_str(), request.uri.c_str());

    if (request.method == "OPTIONS") {
        SendResponse(session->connection_id, 200, request,
                     {{"Public", "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN"}},
                     "");
        return;
    }

    // DESCRIBE/SETUP 从 URI 解析 stream；PLAY/TEARDOWN 使用 session 中已经
    // SETUP 的 stream，避免客户端在 PLAY 阶段换 URI 绕过状态机。
    StreamId stream_id = session->stream_id;
    if ((request.method == "DESCRIBE" || request.method == "SETUP") &&
        !PathToStreamId(request.uri, &stream_id)) {
        Error(kRtspRequestHandlerModule,
              "RTSP path not found uri=%s", request.uri.c_str());
        SendResponse(session->connection_id, 404, request, {}, "");
        return;
    }
    if ((request.method == "DESCRIBE" || request.method == "SETUP") &&
        !delegate_->IsRtspStreamAvailable(stream_id)) {
        Error(kRtspRequestHandlerModule,
              "RTSP stream unavailable uri=%s", request.uri.c_str());
        SendResponse(session->connection_id, 404, request, {}, "");
        return;
    }

    if (!delegate_->AuthorizeRtspRequest(session, request, stream_id)) {
        return;
    }

    if (request.method == "DESCRIBE") {
        HandleDescribe(session, request, stream_id);
        return;
    }
    if (request.method == "SETUP") {
        (void)delegate_->SetupRtspTransport(session, request, stream_id);
        return;
    }
    if (request.method == "PLAY") {
        HandlePlay(session, request);
        return;
    }
    if (request.method == "TEARDOWN") {
        HandleTeardown(session, request);
        return;
    }

    SendResponse(session->connection_id, 455, request, {}, "");
}

void RtspRequestHandler::SendResponse(
    ConnectionId connection_id, int status,
    const rtsp_internal::RtspRequest &request,
    const std::map<std::string, std::string> &headers,
    const std::string &body) {
    if (delegate_ != nullptr) {
        delegate_->SendRtspResponse(connection_id, status, CSeq(request),
                                    headers, body);
    }
}

void RtspRequestHandler::HandleDescribe(
    const std::shared_ptr<RtspSession> &session,
    const rtsp_internal::RtspRequest &request, StreamId stream_id) {
    session->MarkDescribed(stream_id);
    // DESCRIBE 只生成 SDP，不创建长期 subscription。stream_info 未 ready 返回 455，
    // 让客户端稍后重试，而不是暴露一个无法播放的 SDP。
    MediaStreamInfo stream_info = delegate_->RtspStreamInfoForStream(stream_id);
    if (!stream_info.track_ready) {
        Error(kRtspRequestHandlerModule,
              "RTSP describe stream not ready uri=%s", request.uri.c_str());
        SendResponse(session->connection_id, 455, request, {}, "");
        return;
    }
    const std::string sdp = RtspMuxer::BuildSdp(delegate_->RtspLocalAddress(),
                                                stream_id, stream_info);
    SendResponse(session->connection_id, 200, request,
                 {{"Content-Type", "application/sdp"},
                  {"Content-Base", request.uri}},
                 sdp);
}

void RtspRequestHandler::HandlePlay(
    const std::shared_ptr<RtspSession> &session,
    const rtsp_internal::RtspRequest &request) {
    if (!session->IsReadyForPlay()) {
        SendResponse(session->connection_id, 455, request, {}, "");
        return;
    }
    // StartRtspMediaStream 会 attach keyframe_first subscription 并准备启动 GOP；
    // 200 响应发出后才 arm drain timer，避免客户端未收到 PLAY 成功就收到 RTP。
    const int stream_status = delegate_->StartRtspMediaStream(session);
    if (stream_status != 200) {
        SendResponse(session->connection_id, stream_status, request, {}, "");
        return;
    }
    Info(kRtspRequestHandlerModule,
         "RTSP play conn=%llu stream=%s transport=%s",
         static_cast<unsigned long long>(session->connection_id),
         StreamPath(session->stream_id),
         session->transport == RtspTransportMode::kTcpInterleaved
             ? "tcp"
             : "udp");
    SendResponse(session->connection_id, 200, request,
                 {{"Session", std::to_string(session->session_id)},
                  {"Range", "npt=0.000-"},
                  {"RTP-Info", BuildRtpInfo(session, request)}},
                 "");
    delegate_->ArmRtspMediaStream(session);
}

void RtspRequestHandler::HandleTeardown(
    const std::shared_ptr<RtspSession> &session,
    const rtsp_internal::RtspRequest &request) {
    SendResponse(session->connection_id, 200, request,
                 {{"Session", std::to_string(session->session_id)}}, "");
    session->Close();
    delegate_->CloseRtspConnectionAfterSend(session->connection_id);
}

}  // namespace live_stream
