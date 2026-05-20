#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "infra/log.h"
#include "net_service.h"
#include "stream_hub_service.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace live_stream {
namespace {

constexpr uint32_t kFlvBootstrapWaitMs = 2500;
constexpr uint32_t kFlvBootstrapPollMs = 50;

const char *VideoCodecName(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::kH264:
            return "h264";
        case VideoCodec::kH265:
            return "h265";
        case VideoCodec::kMjpeg:
            return "mjpeg";
        case VideoCodec::kJpeg:
            return "jpeg";
    }
    return "unknown";
}

bool RequestBrowserKeyFrame(IStreamHubService *stream_hub_service,
                            StreamId stream_id) {
    return stream_hub_service != nullptr &&
           stream_hub_service->RequestKeyFrame(stream_id,
                                               KeyFrameReason::kRecovery);
}

class FlvConnectionSink : public IStreamFlvSink {
public:
    FlvConnectionSink(HttpHandlerContext *context, ConnectionId connection_id)
        : context_(context), connection_id_(connection_id) {}

    bool OnFlvChunk(const uint8_t *data, size_t size) override {
        return context_ != nullptr &&
               context_->EnqueueStreamingChunk(connection_id_, data, size);
    }

private:
    HttpHandlerContext *context_ = nullptr;
    ConnectionId connection_id_ = 0;
};

void SendFlvError(HttpHandlerContext *context, ConnectionId connection_id,
                  const HttpResponse &response) {
    context->SendResponse(connection_id, response, true);
}

bool ParseFlvStreamName(const HttpRequest &request, StreamId *stream_id,
                        std::string *stream_name) {
    if (stream_id == nullptr || stream_name == nullptr) {
        return false;
    }
    *stream_name = PathSuffix(request.path, "/api/flv/");
    if (stream_name->size() <= 4 ||
        stream_name->substr(stream_name->size() - 4) != ".flv") {
        return false;
    }
    stream_name->resize(stream_name->size() - 4);
    return StreamIdFromJsonString(*stream_name, stream_id);
}

}  // namespace

void http_handlers::StartFlvStream(HttpHandlerContext *context,
                                   ConnectionId connection_id,
                                   const HttpRequest &request) {
    if (context->Dependencies().stream_hub_service == nullptr) {
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HTTP-FLV reject conn=%llu reason=no_stream_hub",
                        static_cast<unsigned long long>(connection_id));
        SendFlvError(context, connection_id,
                     StatusResponse(501, "Not Implemented"));
        return;
    }
    if (IsMediaRestarting(context)) {
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HTTP-FLV reject conn=%llu reason=media_restarting",
                        static_cast<unsigned long long>(connection_id));
        SendFlvError(context, connection_id,
                     StatusResponse(503, "Media pipeline restarting"));
        return;
    }
    AuthPrincipal principal;
    if (!RequireAuth(context, request, &principal)) {
        INFRA_LOG_ERROR(kHttpModuleName, "HTTP-FLV reject conn=%llu reason=auth",
                        static_cast<unsigned long long>(connection_id));
        SendFlvError(context, connection_id,
                     StatusResponse(401, "Unauthorized"));
        return;
    }

    StreamId stream_id = StreamId::kMain;
    std::string stream_name;
    if (!ParseFlvStreamName(request, &stream_id, &stream_name)) {
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HTTP-FLV reject conn=%llu reason=path path=%s",
                        static_cast<unsigned long long>(connection_id),
                        request.path.c_str());
        SendFlvError(context, connection_id,
                     StatusResponse(400, "Invalid FLV path"));
        return;
    }

    const StreamBrowserStatus browser_status =
        context->Dependencies().stream_hub_service->GetBrowserStatus(stream_id);
    if (!browser_status.browser_codec) {
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HTTP-FLV reject conn=%llu stream=%s "
                        "reason=unsupported codec=%s running=%d flv_ready=%d",
                        static_cast<unsigned long long>(connection_id),
                        StreamIdToJsonString(stream_id),
                        VideoCodecName(browser_status.codec),
                        browser_status.running ? 1 : 0,
                        browser_status.flv_ready ? 1 : 0);
        SendFlvError(
            context, connection_id,
            StatusResponse(409, "HTTP-FLV requires H.264 or H.265 stream"));
        return;
    }
    if (!browser_status.running || !browser_status.browser_codec) {
        const bool keyframe_requested =
            browser_status.running &&
            RequestBrowserKeyFrame(context->Dependencies().stream_hub_service,
                                   stream_id);
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HTTP-FLV reject conn=%llu stream=%s reason=not_ready "
                        "codec=%s running=%d flv_ready=%d keyframe=%d",
                        static_cast<unsigned long long>(connection_id),
                        StreamIdToJsonString(stream_id),
                        VideoCodecName(browser_status.codec),
                        browser_status.running ? 1 : 0,
                        browser_status.flv_ready ? 1 : 0,
                        keyframe_requested ? 1 : 0);
        SendFlvError(context, connection_id,
                     StatusResponse(503, "FLV stream not ready"));
        return;
    }

    IStreamHubService *stream_hub =
        context->Dependencies().stream_hub_service;
    StreamFlvBootstrap bootstrap = stream_hub->GetFlvBootstrap(stream_id);
    if ((!bootstrap.supported || bootstrap.sequence_header.empty()) &&
        browser_status.running && browser_status.browser_codec) {
        (void)RequestBrowserKeyFrame(stream_hub, stream_id);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(kFlvBootstrapWaitMs);
        while ((!bootstrap.supported || bootstrap.sequence_header.empty()) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kFlvBootstrapPollMs));
            bootstrap = stream_hub->GetFlvBootstrap(stream_id);
        }
    }
    if (!bootstrap.supported || bootstrap.file_header.empty() ||
        bootstrap.sequence_header.empty()) {
        const bool keyframe_requested =
            RequestBrowserKeyFrame(stream_hub, stream_id);
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HTTP-FLV reject conn=%llu stream=%s "
                        "reason=bootstrap codec=%s running=%d flv_ready=%d "
                        "keyframe=%d",
                        static_cast<unsigned long long>(connection_id),
                        StreamIdToJsonString(stream_id),
                        VideoCodecName(browser_status.codec),
                        browser_status.running ? 1 : 0,
                        browser_status.flv_ready ? 1 : 0,
                        keyframe_requested ? 1 : 0);
        SendFlvError(context, connection_id,
                     StatusResponse(503, "FLV stream not ready"));
        return;
    }

    std::shared_ptr<IStreamFlvSink> sink(
        new FlvConnectionSink(context, connection_id));
    if (!context->BeginFlvSession(connection_id, sink)) {
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HTTP-FLV close conn=%llu stream=%s reason=no_session",
                        static_cast<unsigned long long>(connection_id),
                        StreamIdToJsonString(stream_id));
        return;
    }

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "video/x-flv";
    headers["Cache-Control"] = "no-cache";
    headers["Pragma"] = "no-cache";
    const std::string header_block = BuildStreamingHeaderBlock(200, headers);
    INFRA_LOG_INFO(kHttpModuleName,
                   "HTTP-FLV start conn=%llu stream=%s client=%llu header=%zu "
                   "file=%zu sequence=%zu keyframe=%zu",
                   static_cast<unsigned long long>(connection_id),
                   StreamIdToJsonString(stream_id),
                   static_cast<unsigned long long>(0), header_block.size(),
                   bootstrap.file_header.size(),
                   bootstrap.sequence_header.size(),
                   bootstrap.last_keyframe.size());
    if (!context->EnqueueStreamingChunk(
            connection_id,
            reinterpret_cast<const uint8_t *>(header_block.data()),
            header_block.size()) ||
        !context->EnqueueStreamingChunk(
            connection_id,
            reinterpret_cast<const uint8_t *>(bootstrap.file_header.data()),
            bootstrap.file_header.size()) ||
        (!bootstrap.sequence_header.empty() &&
         !context->EnqueueStreamingChunk(
             connection_id,
             reinterpret_cast<const uint8_t *>(
                 bootstrap.sequence_header.data()),
             bootstrap.sequence_header.size())) ||
        (!bootstrap.last_keyframe.empty() &&
         !context->EnqueueStreamingChunk(
             connection_id,
             reinterpret_cast<const uint8_t *>(bootstrap.last_keyframe.data()),
             bootstrap.last_keyframe.size()))) {
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HTTP-FLV close conn=%llu stream=%s reason=enqueue",
                        static_cast<unsigned long long>(connection_id),
                        StreamIdToJsonString(stream_id));
        context->CloseConnection(connection_id);
        return;
    }

    const StreamFlvClientId client_id =
        stream_hub->AttachFlvClient(
            stream_id, bootstrap.config_generation, sink);
    if (client_id == 0) {
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HTTP-FLV close conn=%llu stream=%s reason=attach",
                        static_cast<unsigned long long>(connection_id),
                        StreamIdToJsonString(stream_id));
        context->CloseConnection(connection_id);
        return;
    }

    if (!context->AttachFlvSessionClient(connection_id, client_id)) {
        (void)stream_hub->DetachFlvClient(client_id);
        INFRA_LOG_ERROR(kHttpModuleName,
                        "HTTP-FLV close conn=%llu stream=%s reason=closed",
                        static_cast<unsigned long long>(connection_id),
                        StreamIdToJsonString(stream_id));
        return;
    }
    INFRA_LOG_INFO(kHttpModuleName,
                   "HTTP-FLV attached conn=%llu stream=%s client=%llu",
                   static_cast<unsigned long long>(connection_id),
                   StreamIdToJsonString(stream_id),
                   static_cast<unsigned long long>(client_id));
}

}  // namespace live_stream
