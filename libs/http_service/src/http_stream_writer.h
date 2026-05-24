#ifndef LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_STREAM_WRITER_H_
#define LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_STREAM_WRITER_H_

#include "http_service.h"
#include "media/media_buffer.h"
#include "net_service.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace live_stream {

using HttpStreamClientId = uint64_t;
using HttpStreamCloseCallback = std::function<void(HttpStreamClientId)>;

struct HttpStreamSlice {
    const uint8_t *data = nullptr;
    size_t size = 0;
    VideoBuffer *owner = nullptr;
};

// Streaming response boundary for long-lived HTTP outputs such as FLV and
// future SSE event streams.
class HttpStreamWriter {
public:
    virtual ~HttpStreamWriter() = default;

    virtual void SendResponse(ConnectionId connection_id,
                              const HttpResponse &response,
                              bool close_after_response) = 0;
    virtual bool BeginStream(ConnectionId connection_id) = 0;
    virtual bool AttachStreamClient(ConnectionId connection_id,
                                    HttpStreamClientId client_id) = 0;
    virtual bool EnqueueStreamingChunk(ConnectionId connection_id,
                                       const uint8_t *data, size_t size) = 0;
    // Slices with owner may outlive this call; the writer retains the owner
    // until network send completion. Slices without owner must be small
    // protocol bytes that can be copied into the TCP output queue.
    virtual bool EnqueueStreamingSlices(ConnectionId connection_id,
                                        const HttpStreamSlice *slices,
                                        size_t slice_count) = 0;
    virtual void SetCloseCallback(HttpStreamCloseCallback callback) = 0;
    virtual void CloseConnection(ConnectionId connection_id) = 0;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_SRC_HTTP_STREAM_WRITER_H_
