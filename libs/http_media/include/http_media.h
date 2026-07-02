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

std::unique_ptr<IHttpHandler> CreateHlsHttpHandler(
    HttpAccess *access,
    DeviceMedia *device,
    MediaStreams *media_streams);
std::unique_ptr<IHttpHandler> CreateWebrtcHttpHandler(
    HttpAccess *access,
    DeviceMedia *device,
    IWebrtc *webrtc);

std::unique_ptr<IStreamingHttpHandler> CreateStreamingHttpHandler(
    HttpAccess *access,
    HttpMediaWriter *writer,
    DeviceMedia *device,
    MediaStreams *media_streams,
    event::EventCenter *event);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_H_
