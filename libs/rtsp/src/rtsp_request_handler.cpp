#include "rtsp_request_handler.h"

#include "infra/log.h"

namespace live_stream {
namespace {

constexpr const char *kRtspRequestHandlerModule = "rtsp";

}  // namespace

using rtsp_internal::BuildSdp;
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
  const std::string sdp = BuildSdp(delegate_->RtspLocalAddress(), stream_id,
                                   delegate_->RtspCodecForStream(stream_id));
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
  session->StartPlaying();
  if (delegate_->RequestRtspKeyFrame(session->stream_id)) {
    delegate_->OnRtspKeyFrameRequested(*session);
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
                {"RTP-Info",
                 "url=" + std::string(StreamPath(session->stream_id))}},
               "");
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
