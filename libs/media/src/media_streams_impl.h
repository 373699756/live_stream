#ifndef LIVE_STREAM_MEDIA_SRC_MEDIA_STREAMS_IMPL_H_
#define LIVE_STREAM_MEDIA_SRC_MEDIA_STREAMS_IMPL_H_

#include "media/media_streams.h"

#include "frame_subscribers.h"
#include "media_stream_tracks.h"
#include "preview_clients.h"

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace live_stream {

enum class MediaStreamsPhase {
    kStopped = 0,
    kRunning,
    kStopping,
};

class MediaStreams::Impl {
public:
    explicit Impl(MediaStreamsOptions options);
    ~Impl();

    bool Start();
    void Stop();
    bool PushFrame(const MediaFrame &frame);
    void SetStreamState(StreamId stream_id, MediaStreamState state,
                        Codec codec);

    bool IsHlsSupported(StreamId stream_id) const;
    bool IsFlvSupported(StreamId stream_id) const;
    bool IsMjpegSupported(StreamId stream_id) const;
    bool IsStreamAvailable(StreamId stream_id) const;
    Codec GetStreamCodec(StreamId stream_id) const;
    MediaHlsPlaylist GetHlsPlaylist(StreamId stream_id) const;
    MediaSegmentRef GetHlsSegmentRef(StreamId stream_id,
                                     uint64_t sequence) const;
    MediaFlvStart GetFlvStart(StreamId stream_id) const;
    MediaStreamInfo GetStreamInfo(StreamId stream_id) const;
    MediaStreamStats GetStreamStats() const;

    bool RequestKeyframe(StreamId stream_id,
                         KeyframeRequestSource source);
    MediaFlvClientId AttachFlvClient(StreamId stream_id,
                                     uint64_t config_generation,
                                     bool wait_for_keyframe,
                                     IMediaFlvSink *sink);
    bool DetachFlvClient(MediaFlvClientId client_id);
    MediaMjpegClientId AttachMjpegClient(StreamId stream_id,
                                         IMediaMjpegSink *sink);
    bool DetachMjpegClient(MediaMjpegClientId client_id);

    FrameSubscriptionId SubscribeFrames(
        const SubscriptionOptions &options);
    bool UnsubscribeFrames(FrameSubscriptionId subscription_id,
                           SubscriptionClose reason);
    SubscriptionInfo GetSubscriptionInfo(
        FrameSubscriptionId subscription_id) const;
    SubscriptionStart GetSubscriptionStart(
        FrameSubscriptionId subscription_id) const;
    bool PullFrame(FrameSubscriptionId subscription_id,
                   SubscriptionFrame *frame);

private:
    template <typename Value, typename Reader>
    Value ReadStream(StreamId stream_id, Value fallback, Reader reader) const {
        std::shared_lock<std::shared_mutex> guard(mutex_);
        const media_internal::StreamTrack *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return fallback;
        }
        return reader(*stream);
    }

    static constexpr const char *kLogModuleName = "media";

    static uint32_t HlsSegmentCacheDepth(
        const MediaStreamsOptions &options);
    static media_internal::FrameSubscribersOptions BuildFrameSubscribersOptions(
        const MediaCacheLimits &limits);
    static media_internal::StreamTrackCacheOptions BuildStreamCacheOptions(
        const MediaStreamsOptions &options);
    static bool IsStreamSupported(StreamId stream_id);
    static const char *StreamIdName(StreamId stream_id);
    static const char *CodecName(Codec codec);
    static const char *MediaStreamStateName(MediaStreamState state);
    static const char *PhaseName(MediaStreamsPhase state);
    static const char *KeyframeRequestSourceName(
        KeyframeRequestSource source);
    static SubscriptionClose CloseReasonFromReset(
        MediaStreamResetReason reason);
    static uint8_t SubscriptionEventLevel(const char *msg);

    bool ValidateOptions() const;
    bool ValidateCacheLimits() const;
    bool IsRunningLocked() const;
    void ConfigureMediaCachesLocked();
    void ResetStreamsLocked();
    void ApplyResetNoticeLocked(
        const media_internal::StreamResetNotice &notice);
    void EnsureRunningStreamLocked(media_internal::StreamTrack &stream,
                                   StreamId stream_id,
                                   Codec codec);
    bool AcceptFrame(
        MediaFrame &frame,
        media_internal::ParsedFramePayload &parsed_payload);
    bool NormalizeFrameTimestampLocked(
        media_internal::StreamTrack &stream,
        MediaFrame &frame);
    media_internal::ParsedFramePayload ParseFramePayloadView(
        const MediaFrame &frame) const;
    void CachePreviewFrame(
        const media_internal::ParsedFramePayload &parsed_payload,
        bool has_payload);
    void PackageMjpegFrame(
        const media_internal::ParsedFramePayload &parsed_payload);
    void PublishSubscriptionEvent(StreamId stream_id, const char *msg,
                                  uint32_t active_subscriptions) const;

    const media_internal::StreamTrack *FindStream(
        StreamId stream_id) const;
    media_internal::StreamTrack &MutableStreamTrackFor(StreamId stream_id);
    void SetStreamStateLocked(StreamId stream_id, MediaStreamState state,
                              Codec codec);

    MediaStreamsOptions options_;
    mutable std::shared_mutex mutex_;
    MediaStreamsPhase phase_ = MediaStreamsPhase::kStopped;
    media_internal::MediaStreamTracks streams_;
    media_internal::FrameSubscribers frame_subscribers_;
    media_internal::PreviewClients preview_clients_;
    MediaStreamStats stats_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_MEDIA_STREAMS_IMPL_H_
