#include "http_media.h"

namespace live_stream {

std::unique_ptr<IHttpHandler> MakeHlsHandler(
    HttpAccess *access, DeviceMedia *device,
    MediaStreams *media_streams);
std::unique_ptr<IHttpHandler> MakeWebrtcHandler(
    HttpAccess *access, DeviceMedia *device,
    IWebrtc *webrtc);

std::unique_ptr<IHttpHandler> CreateHttpHandler(
    HttpMediaHandlerKind kind,
    const HttpMediaHandlerDependencies &dependencies) {
    switch (kind) {
        case HttpMediaHandlerKind::kHls:
            return MakeHlsHandler(
                dependencies.access, dependencies.device,
                dependencies.media_streams);
        case HttpMediaHandlerKind::kWebrtc:
            return MakeWebrtcHandler(
                dependencies.access, dependencies.device,
                dependencies.webrtc);
    }
    return nullptr;
}

}  // namespace live_stream
