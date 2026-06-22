#ifndef LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_H_
#define LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_H_

#include "http.h"
#include "http_access.h"
#include "http_media_writer.h"
#include "media/media_streams.h"

#include <memory>

namespace live_stream {

class DeviceMedia;
class IWebrtc;

class IStreamingHttpHandler {
public:
    virtual ~IStreamingHttpHandler() = default;

    virtual bool CanHandleStreamingRequest(const HttpRequest &request) const = 0;
    virtual HttpStreamingRequestResult HandleStreamingRequest(
        ConnectionId connection_id, const HttpRequest &request) = 0;
};

enum class HttpMediaHandlerKind {
    kHls = 0,
    kWebrtc,
};

struct HttpMediaHandlerDependencies {
    HttpAccess *access = nullptr;
    DeviceMedia *device = nullptr;
    MediaStreams *media_streams = nullptr;
    IWebrtc *webrtc = nullptr;
};

std::unique_ptr<IHttpHandler> CreateHttpHandler(
    HttpMediaHandlerKind kind,
    const HttpMediaHandlerDependencies &dependencies);

struct StreamingHttpHandlerDependencies {
    HttpAccess *access = nullptr;
    HttpMediaWriter *writer = nullptr;
    DeviceMedia *device = nullptr;
    MediaStreams *media_streams = nullptr;
    event::Dispatcher *event = nullptr;
};

std::unique_ptr<IStreamingHttpHandler> CreateStreamingHttpHandler(
    const StreamingHttpHandlerDependencies &dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_H_
