#ifndef LIVE_STREAM_MEDIA_SRC_MEDIA_STREAM_TRACKS_H_
#define LIVE_STREAM_MEDIA_SRC_MEDIA_STREAM_TRACKS_H_

#include "media_stream_state.h"

namespace live_stream {
namespace media_internal {

struct StreamResetNotice {
    bool reset = false;
    StreamId stream_id = StreamId::kMain;
    Codec codec = Codec::kH264;
    MediaStreamResetReason reason = MediaStreamResetReason::kNone;
};

// Owns the two encoded stream tracks and the reset rules that keep codec,
// timestamp, HLS, FLV, and MJPEG state in the same generation.
class MediaStreamTracks {
public:
    void Clear();

    const StreamTrack *Find(StreamId stream_id) const;
    StreamTrack *FindMutable(StreamId stream_id);

    bool EnsureRunning(StreamId stream_id, Codec codec,
                       StreamResetNotice *notice);
    void SetState(StreamId stream_id, MediaStreamState state, Codec codec,
                  StreamResetNotice *notice);
    void Reset(StreamId stream_id, Codec codec, MediaStreamResetReason reason,
               StreamResetNotice *notice);

    const StreamTrack &main_stream() const { return main_stream_; }
    const StreamTrack &sub_stream() const { return sub_stream_; }

private:
    static void FillResetNotice(StreamResetNotice *notice, StreamId stream_id,
                                Codec codec, MediaStreamResetReason reason);

    StreamTrack main_stream_;
    StreamTrack sub_stream_;
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_MEDIA_STREAM_TRACKS_H_
