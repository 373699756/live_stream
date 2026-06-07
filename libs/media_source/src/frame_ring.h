#ifndef LIVE_STREAM_MEDIA_SOURCE_SRC_FRAME_RING_H_
#define LIVE_STREAM_MEDIA_SOURCE_SRC_FRAME_RING_H_

#include "media_source.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace live_stream {
namespace media_source_internal {

class FrameRing {
public:
    FrameRing() = default;
    FrameRing(const FrameRing &) = delete;
    FrameRing &operator=(const FrameRing &) = delete;
    ~FrameRing();

    MediaFrameReaderId AttachReader(const MediaFrameReaderOptions &options,
                                    size_t max_readers);
    bool DetachReader(MediaFrameReaderId reader_id,
                      MediaFrameReaderCloseReason reason);
    MediaFrameReaderStatus GetReaderStatus(MediaFrameReaderId reader_id) const;
    MediaFrameReaderStartData GetStartData(MediaFrameReaderId reader_id,
                                           const MediaTrack &track) const;
    bool PopFrame(MediaFrameReaderId reader_id,
                  MediaFrameReaderFrame *frame);
    void Clear();
    void ClearStream(StreamId stream_id, MediaFrameReaderCloseReason reason);
    size_t ReaderCount() const;
    uint32_t SlowReaderCount() const;
    uint32_t SlowReaderCount(StreamId stream_id) const;
    uint32_t CachedFrameCount() const;
    uint32_t CachedBytes() const;
    int64_t LastFrameTimestamp(StreamId stream_id) const;
    void Write(const FramePayload &frame);

private:
    static constexpr size_t kMaxCachedGopFrames = 128;
    static constexpr size_t kMaxQueuedLiveFrames = 32;

    struct CachedFrame {
        uint64_t sequence = 0;
        FramePayload payload;
        bool key_frame = false;
        int64_t duration_us = 0;
        uint32_t bytes = 0;
    };

    struct StreamCache {
        std::array<CachedFrame, kMaxCachedGopFrames> frames;
        size_t size = 0;
        uint32_t bytes = 0;
        bool complete = false;
        int64_t last_frame_timestamp_us = 0;
        int64_t last_dts_us = -1;
        uint64_t generation = 0;
    };

    struct QueuedFrame {
        uint64_t sequence = 0;
        FramePayload payload;
        bool key_frame = false;
        bool starts_on_keyframe = false;
        int64_t duration_us = 0;
    };

    struct LiveQueue {
        std::array<QueuedFrame, kMaxQueuedLiveFrames> frames;
        size_t head = 0;
        size_t size = 0;
        bool overflow = false;
    };

    struct ReaderState {
        StreamId stream_id = StreamId::kMain;
        bool keyframe_first = true;
        bool waiting_for_keyframe = true;
        std::string reader_name;
        uint64_t start_sequence = 0;
        uint64_t start_generation = 0;
        uint64_t next_sequence = 0;
        uint64_t generation = 0;
        LiveQueue live_queue;
        MediaFrameReaderCloseReason close_reason =
            MediaFrameReaderCloseReason::kNone;
    };

    static StreamCache *FindCache(StreamId stream_id,
                                  StreamCache *main_cache,
                                  StreamCache *sub_cache);
    static const StreamCache *FindCache(StreamId stream_id,
                                        const StreamCache *main_cache,
                                        const StreamCache *sub_cache);
    static void ClearCache(StreamCache *cache);
    static void ClearLiveQueue(LiveQueue *queue);
    static bool PushLiveQueue(LiveQueue *queue, uint64_t sequence,
                              bool key_frame, bool starts_on_keyframe,
                              int64_t duration_us,
                              const FramePayload &frame);
    static bool PopLiveQueue(LiveQueue *queue, QueuedFrame *frame);
    static bool ToMediaFrame(const FramePayload &payload, bool key_frame,
                             int64_t duration_us, MediaFrame *frame);
    static uint32_t CachedFrameBytes(const CachedFrame &frame);
    static int64_t EstimateFrameDuration(const StreamCache &cache,
                                         const FramePayload &frame);

    bool AppendToCache(StreamCache *cache, uint64_t sequence,
                       bool key_frame, int64_t duration_us,
                       const FramePayload &frame);
    void ResetReaderForStream(ReaderState *reader, const StreamCache &cache,
                              MediaFrameReaderCloseReason reason);

    std::map<MediaFrameReaderId, ReaderState> readers_;
    MediaFrameReaderId next_reader_id_ = 1;
    uint64_t next_sequence_ = 1;
    StreamCache main_cache_;
    StreamCache sub_cache_;
};

}  // namespace media_source_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_SRC_FRAME_RING_H_
