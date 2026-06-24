#ifndef LIVE_STREAM_MEDIA_SRC_PREVIEW_CLIENTS_H_
#define LIVE_STREAM_MEDIA_SRC_PREVIEW_CLIENTS_H_

#include "flv_muxer.h"
#include "flv_clients.h"
#include "mjpeg_clients.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace live_stream {
namespace media_internal {

// Owns live preview clients for HTTP-FLV and MJPEG. Sink callbacks run without
// the MediaStreams lock; pending-write counters keep detached sinks alive until
// the callback returns.
class PreviewClients {
public:
    void Clear();

    size_t FlvSize() const;
    size_t MjpegSize() const;

    MediaFlvClientId AttachFlv(StreamId stream_id,
                               uint64_t config_generation,
                               bool wait_for_keyframe,
                               IMediaFlvSink *sink,
                               size_t max_clients);
    bool DetachFlv(MediaFlvClientId client_id);
    bool HasFlvClient(StreamId stream_id) const;
    void WriteFlv(StreamId stream_id,
                  uint64_t config_generation,
                  bool keyframe,
                  const std::string &sequence_header_tag,
                  const FlvVideoTagBuild &flv_tag_view,
                  bool has_flv_tag_view,
                  const MediaFrame &frame);

    MediaMjpegClientId AttachMjpeg(StreamId stream_id,
                                   IMediaMjpegSink *sink,
                                   size_t max_clients);
    bool DetachMjpeg(MediaMjpegClientId client_id);
    bool HasMjpegClient(StreamId stream_id) const;
    void WriteMjpeg(StreamId stream_id, const MediaFrame &frame);

private:
    class FlvWriteLease;
    class MjpegWriteLease;

    void ReleaseFlvWrite(MediaFlvClientId client_id);
    void ReleaseMjpegWrite(MediaMjpegClientId client_id);

    mutable std::mutex mutex_;
    FlvClients flv_clients_;
    MjpegClients mjpeg_clients_;
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_PREVIEW_CLIENTS_H_
