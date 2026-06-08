#ifndef LIVE_STREAM_MEDIA_SOURCE_SRC_FLV_LIVE_RING_H_
#define LIVE_STREAM_MEDIA_SOURCE_SRC_FLV_LIVE_RING_H_

#include "media_source.h"

#include <cstddef>
#include <map>
#include <vector>

namespace live_stream {
namespace media_source_internal {

struct PendingFlvClientWrite {
    MediaFlvClientId client_id = 0;
    IMediaFlvSink *sink = nullptr;
    bool send_sequence_header = false;
    bool starts_on_keyframe = false;
};

// Tracks HTTP-FLV live readers for one media source. The owning service provides
// synchronization and calls this class under its mutex.
class FlvLiveRing {
public:
    MediaFlvClientId AttachReader(StreamId stream_id,
                                  uint64_t config_generation,
                                  bool wait_for_keyframe,
                                  IMediaFlvSink *sink,
                                  size_t max_readers);
    bool DetachReader(MediaFlvClientId client_id);
    void Clear();
    size_t ReaderCount() const;
    bool HasReader(StreamId stream_id) const;
    std::vector<PendingFlvClientWrite> CollectWrites(
        StreamId stream_id, uint64_t config_generation, bool has_flv_tag,
        bool has_sequence_header, bool keyframe);
    void ReleaseWrite(MediaFlvClientId client_id);

private:
    struct ReaderState {
        StreamId stream_id = StreamId::kMain;
        uint64_t config_generation = 0;
        bool wait_for_keyframe = false;
        IMediaFlvSink *sink = nullptr;
        uint32_t pending_writes = 0;
        bool detached = false;
    };

    static void ReleaseReaderSink(ReaderState *reader);
    bool EraseDetachedReader(MediaFlvClientId client_id,
                             ReaderState *reader);

    std::map<MediaFlvClientId, ReaderState> readers_;
    MediaFlvClientId next_reader_id_ = 1;
};

}  // namespace media_source_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_SRC_FLV_LIVE_RING_H_
