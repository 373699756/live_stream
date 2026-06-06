#ifndef LIVE_STREAM_MEDIA_SOURCE_SRC_FRAME_RING_H_
#define LIVE_STREAM_MEDIA_SOURCE_SRC_FRAME_RING_H_

#include "media/frame_attach.h"

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace live_stream {
namespace media_source_internal {

struct PendingFrameRingWrite {
    FrameAttachId reader_id = 0;
    IFrameSink *sink = nullptr;
    bool starts_on_keyframe = false;
};

class FrameRing {
public:
    FrameRing() = default;
    FrameRing(const FrameRing &) = delete;
    FrameRing &operator=(const FrameRing &) = delete;
    ~FrameRing();

    FrameAttachId AttachReader(const FrameAttachOptions &options,
                               IFrameSink *sink, size_t max_readers);
    bool DetachReader(FrameAttachId reader_id);
    void Clear();
    void ClearStreamCache(StreamId stream_id);
    size_t ReaderCount() const;
    std::vector<PendingFrameRingWrite> Write(const FramePayload &frame);

private:
    static constexpr size_t kMaxCachedGopFrames = 128;

    struct CachedFrame {
        uint64_t sequence = 0;
        FramePayload payload;
        bool key_frame = false;
    };

    struct StreamCache {
        std::array<CachedFrame, kMaxCachedGopFrames> frames;
        size_t size = 0;
        bool complete = false;
    };

    struct ReaderState {
        StreamId stream_id = StreamId::kMain;
        bool require_key_frame_first = true;
        IFrameSink *sink = nullptr;
        std::string sink_name;
        uint64_t next_sequence = 0;
    };

    static StreamCache *FindCache(StreamId stream_id,
                                  StreamCache *main_cache,
                                  StreamCache *sub_cache);
    static const StreamCache *FindCache(StreamId stream_id,
                                        const StreamCache *main_cache,
                                        const StreamCache *sub_cache);
    static void ClearCache(StreamCache *cache);

    bool AppendToCache(StreamCache *cache, uint64_t sequence,
                       bool key_frame, const FramePayload &frame);

    std::map<FrameAttachId, ReaderState> readers_;
    FrameAttachId next_reader_id_ = 1;
    uint64_t next_sequence_ = 1;
    StreamCache main_cache_;
    StreamCache sub_cache_;
};

}  // namespace media_source_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_SRC_FRAME_RING_H_
