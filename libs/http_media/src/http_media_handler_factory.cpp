#include "http_media.h"

namespace live_stream {

std::unique_ptr<IHttpHandler> MakeHlsHandler(
    const HttpMediaHandlerDependencies &dependencies);
std::unique_ptr<IHttpHandler> MakeWebrtcHandler(
    const HttpMediaHandlerDependencies &dependencies);

std::unique_ptr<IHttpHandler> CreateHttpHandler(
    HttpMediaHandlerKind kind,
    const HttpMediaHandlerDependencies &dependencies) {
    switch (kind) {
        case HttpMediaHandlerKind::kHls:
            return MakeHlsHandler(dependencies);
        case HttpMediaHandlerKind::kWebrtc:
            return MakeWebrtcHandler(dependencies);
    }
    return nullptr;
}

}  // namespace live_stream
