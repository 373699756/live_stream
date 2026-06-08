#include "http_media.h"

namespace live_stream {

std::unique_ptr<IHttpHandler> MakeHlsHandler(
    HttpAccess *access, IDeviceMedia *device_media,
    IMediaSource *media_source);
std::unique_ptr<IHttpHandler> MakeWebrtcHandler(
    HttpAccess *access, IDeviceMedia *device_media,
    IWebrtc *webrtc);

std::unique_ptr<IHttpHandler> CreateHttpHandler(
    HttpMediaHandlerKind kind,
    const HttpMediaHandlerDependencies &dependencies) {
    switch (kind) {
        case HttpMediaHandlerKind::kHls:
            return MakeHlsHandler(
                dependencies.access, dependencies.device_media,
                dependencies.media_source);
        case HttpMediaHandlerKind::kWebrtc:
            return MakeWebrtcHandler(
                dependencies.access, dependencies.device_media,
                dependencies.webrtc);
    }
    return nullptr;
}

}  // namespace live_stream
