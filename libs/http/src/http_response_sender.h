#ifndef LIVE_STREAM_HTTP_SRC_HTTP_RESPONSE_SENDER_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_RESPONSE_SENDER_H_

#include "http.h"
#include "net.h"

#include <cstddef>
#include <cstdint>

namespace live_stream {

// Owns HTTP response serialization and TCP enqueue policy for one HTTP server.
// HttpServer keeps connection/session lifecycle; this class keeps body slicing,
// FrameBuffer lifetime bridging and slow-client close decisions in one place.
class HttpResponseSender {
public:
    // send_buffer_limit_bytes must be > 0. It sets the maximum outstanding
    // (pending) bytes per streaming connection before the sender closes the
    // connection with kPendingLimit. A value of 0 is rejected by the streaming
    // send path and will cause every write to fail immediately.
    explicit HttpResponseSender(uint32_t send_buffer_limit_bytes);

    bool SendResponse(INetEngine *net_engine, ConnectionId connection_id,
                      const HttpResponse &response,
                      bool close_after_response) const;
    bool SendResponseSlices(INetEngine *net_engine, ConnectionId connection_id,
                            const HttpResponse &response,
                            const MediaSlice *body_slices,
                            size_t body_slice_count, size_t body_size,
                            bool close_after_response) const;
    bool EnqueueStreamingChunk(INetEngine *net_engine,
                               ConnectionId connection_id,
                               const uint8_t *data, size_t size) const;
    bool EnqueueStreamingSlices(INetEngine *net_engine,
                                ConnectionId connection_id,
                                const MediaSlice *slices,
                                size_t slice_count) const;
    void CloseConnection(INetEngine *net_engine, ConnectionId connection_id,
                         TcpCloseReason reason) const;

private:
    bool SendStreamingNetSlices(INetEngine *net_engine,
                                ConnectionId connection_id,
                                const NetBufferSlices &slices,
                                size_t size) const;

    uint32_t send_buffer_limit_bytes_ = 0;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_RESPONSE_SENDER_H_
