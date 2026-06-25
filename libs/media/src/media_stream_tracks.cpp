#include "media_stream_tracks.h"

namespace live_stream {
namespace media_internal {

void MediaStreamTracks::Configure(const StreamTrackCacheOptions &options) {
    ConfigureStreamTrack(main_stream_, options);
    ConfigureStreamTrack(sub_stream_, options);
}

void MediaStreamTracks::Clear() {
    ClearStreamTrack(main_stream_);
    ClearStreamTrack(sub_stream_);
}

const StreamTrack *MediaStreamTracks::Find(StreamId stream_id) const {
    if (stream_id == StreamId::kMain) {
        return &main_stream_;
    }
    if (stream_id == StreamId::kSub) {
        return &sub_stream_;
    }
    return nullptr;
}

StreamTrack &MediaStreamTracks::Mutable(StreamId stream_id) {
    if (stream_id == StreamId::kMain) {
        return main_stream_;
    }
    return sub_stream_;
}

void MediaStreamTracks::EnsureRunning(StreamId stream_id, Codec codec,
                                      StreamResetNotice &notice) {
    StreamTrack &stream = Mutable(stream_id);
    if (stream.state != MediaStreamState::kRunning) {
        Reset(stream_id, codec, MediaStreamResetReason::kStreamStarted, notice);
        stream.codec = codec;
        stream.state = MediaStreamState::kRunning;
        return;
    }
    if (stream.codec != codec) {
        Reset(stream_id, codec, MediaStreamResetReason::kCodecChanged, notice);
        stream.codec = codec;
        stream.state = MediaStreamState::kRunning;
    }
}

void MediaStreamTracks::SetState(StreamId stream_id,
                                 MediaStreamState state,
                                 Codec codec,
                                 StreamResetNotice &notice) {
    StreamTrack &stream = Mutable(stream_id);
    if (state == MediaStreamState::kRunning) {
        EnsureRunning(stream_id, codec, notice);
        return;
    }
    Reset(stream_id, stream.codec, MediaStreamResetReason::kStreamStopped,
          notice);
    stream.state = state;
}

void MediaStreamTracks::Reset(StreamId stream_id,
                              Codec codec,
                              MediaStreamResetReason reason,
                              StreamResetNotice &notice) {
    StreamTrack &stream = Mutable(stream_id);
    ResetStream(stream, codec, reason);
    FillResetNotice(notice, stream_id, codec, reason);
}

void MediaStreamTracks::FillResetNotice(StreamResetNotice &notice,
                                        StreamId stream_id,
                                        Codec codec,
                                        MediaStreamResetReason reason) {
    notice.reset = reason != MediaStreamResetReason::kNone;
    notice.stream_id = stream_id;
    notice.codec = codec;
    notice.reason = reason;
}

}  // namespace media_internal
}  // namespace live_stream
