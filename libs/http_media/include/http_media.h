#ifndef LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_H_
#define LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_H_

#include "http.h"
#include "http_access.h"
#include "http_media_writer.h"
#include "media_source.h"

#include <memory>

namespace live_stream {

class IDeviceMedia;
class IWebrtc;

class IStreamingHttpHandler {
public:
    virtual ~IStreamingHttpHandler() = default;

    virtual bool CanHandleStreamingRequest(const HttpRequest &request) const = 0;
    virtual void HandleStreamingRequest(ConnectionId connection_id,
                                        const HttpRequest &request) = 0;
};

std::unique_ptr<IHttpHandler> CreateHlsHttpHandler(
    HttpAccess *access, IDeviceMedia *device_media,
    IMediaSource *media_source);
std::unique_ptr<IHttpHandler> CreateWebrtcHttpHandler(
    HttpAccess *access, IDeviceMedia *device_media,
    IWebrtc *webrtc);
std::unique_ptr<IStreamingHttpHandler> CreateStreamingHttpHandler(
    HttpAccess *access, HttpMediaWriter *writer, IDeviceMedia *device_media,
    IMediaSource *media_source,
    IMediaFlvSource *media_flv_source,
    IMediaMjpegSource *media_mjpeg_source);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_H_
