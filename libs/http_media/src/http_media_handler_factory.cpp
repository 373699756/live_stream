#include "http_media.h"

namespace live_stream {

std::unique_ptr<IHttpHandler> MakeHlsHandler(
    const HttpMediaHandlerRefs &refs);
std::unique_ptr<IHttpHandler> MakeWebrtcHandler(
    const HttpMediaHandlerRefs &refs);

std::unique_ptr<IHttpHandler> CreateHttpHandler(
    HttpMediaHandlerKind kind,
    const HttpMediaHandlerRefs &refs) {
    switch (kind) {
        case HttpMediaHandlerKind::kHls:
            return MakeHlsHandler(refs);
        case HttpMediaHandlerKind::kWebrtc:
            return MakeWebrtcHandler(refs);
    }
    return nullptr;
}

}  // namespace live_stream
