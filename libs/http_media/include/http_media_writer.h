#ifndef LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_WRITER_H_
#define LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_WRITER_H_

#include "http.h"
#include "media/media_buffer.h"
#include "net.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace live_stream {

enum class HttpMediaClientType {
    kNone,
    kFlv,
    kMjpeg,
};

struct HttpMediaClientHandle {
    HttpMediaClientType type = HttpMediaClientType::kNone;
    uint64_t id = 0;
};

using HttpMediaCloseCallback =
    std::function<void(const HttpMediaClientHandle &)>;

// Streaming response boundary for long-lived HTTP media outputs such as
// HLS segments, HTTP-FLV, and MJPEG.
class HttpMediaWriter {
public:
    virtual ~HttpMediaWriter() = default;

    virtual void SendResponse(ConnectionId connection_id,
                              const HttpResponse &response,
                              bool close_after_response) = 0;
    virtual bool SendResponseSlices(ConnectionId connection_id,
                                    const HttpResponse &response,
                                    const MediaSlice *body_slices,
                                    size_t body_slice_count,
                                    size_t body_size,
                                    bool close_after_response) = 0;
    virtual bool BeginStream(ConnectionId connection_id) = 0;
    virtual bool AttachStreamClient(ConnectionId connection_id,
                                    HttpMediaClientHandle client) = 0;
    virtual bool EnqueueStreamingChunk(ConnectionId connection_id,
                                       const uint8_t *data, size_t size) = 0;
    // Slices with owner may outlive this call; the writer retains the owner
    // until network send completion. Slices without owner must be small
    // protocol bytes that can be copied into the TCP output queue.
    virtual bool EnqueueStreamingSlices(ConnectionId connection_id,
                                        const MediaSlice *slices,
                                        size_t slice_count) = 0;
    virtual void SetCloseCallback(HttpMediaCloseCallback callback) = 0;
    virtual void CloseConnection(ConnectionId connection_id) = 0;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_MEDIA_HTTP_MEDIA_WRITER_H_
