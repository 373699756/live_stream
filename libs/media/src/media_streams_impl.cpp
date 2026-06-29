#include "media_streams_impl.h"

#include "infra/log.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace live_stream {
namespace {

constexpr uint32_t kMinHlsSegmentBytes = 188 * 3;

uint32_t AddCachedBytes(uint32_t current, uint32_t bytes) {
    if (bytes > std::numeric_limits<uint32_t>::max() - current) {
        return std::numeric_limits<uint32_t>::max();
    }
    return current + bytes;
}

}  // namespace

MediaStreams::Impl::Impl(MediaStreamsOptions options)
    : options_(std::move(options)) {}

MediaStreams::Impl::~Impl() { Stop(); }

uint32_t MediaStreams::Impl::HlsSegmentCacheDepth(
    const MediaStreamsOptions &options) {
    return options.hls_playlist_depth + options.hls_segment_retain_size;
}

media_internal::FrameSubscribersOptions
MediaStreams::Impl::BuildFrameSubscribersOptions(
    const MediaCacheLimits &limits) {
    media_internal::FrameSubscribersOptions options;
    options.max_gop_frames = limits.max_subscription_gop_frames;
    options.max_gop_bytes = limits.max_subscription_gop_bytes;
    options.max_shared_frames = limits.max_shared_frames;
    options.max_shared_bytes = limits.max_shared_bytes;
    return options;
}

media_internal::StreamTrackCacheOptions
MediaStreams::Impl::BuildStreamCacheOptions(
    const MediaStreamsOptions &options) {
    const MediaCacheLimits &limits = options.cache_limits;
    media_internal::StreamTrackCacheOptions cache_options;
    cache_options.flv_gop_cache.max_flv_cached_tags =
        limits.max_flv_cached_tags;
    cache_options.flv_gop_cache.max_flv_cached_bytes =
        limits.max_flv_cached_bytes;
    cache_options.hls_maker.max_segments =
        std::min(limits.max_hls_segments, HlsSegmentCacheDepth(options));
    cache_options.hls_maker.max_segment_bytes =
        limits.max_hls_segment_bytes;
    cache_options.hls_maker.max_cached_bytes =
        limits.max_hls_cached_bytes;
    return cache_options;
}

bool MediaStreams::Impl::IsStreamSupported(StreamId stream_id) {
    return stream_id == StreamId::kMain || stream_id == StreamId::kSub;
}

const char *MediaStreams::Impl::StreamIdName(StreamId stream_id) {
    switch (stream_id) {
        case StreamId::kMain:
            return "main";
        case StreamId::kSub:
            return "sub";
        case StreamId::kSnapshot:
            return "snapshot";
    }
    return "unknown";
}

const char *MediaStreams::Impl::CodecName(Codec codec) {
    switch (codec) {
        case Codec::kH264:
            return "h264";
        case Codec::kH265:
            return "h265";
        case Codec::kMjpeg:
            return "mjpeg";
        case Codec::kJpeg:
            return "jpeg";
    }
    return "unknown";
}

const char *MediaStreams::Impl::MediaStreamStateName(
    MediaStreamState state) {
    switch (state) {
        case MediaStreamState::kClosed:
            return "closed";
        case MediaStreamState::kOpening:
            return "opening";
        case MediaStreamState::kRunning:
            return "running";
        case MediaStreamState::kError:
            return "error";
    }
    return "unknown";
}

const char *MediaStreams::Impl::PhaseName(
    MediaStreamsPhase state) {
    switch (state) {
        case MediaStreamsPhase::kStopped:
            return "stopped";
        case MediaStreamsPhase::kRunning:
            return "running";
        case MediaStreamsPhase::kStopping:
            return "stopping";
    }
    return "unknown";
}

const char *MediaStreams::Impl::KeyframeRequestSourceName(
    KeyframeRequestSource source) {
    switch (source) {
        case KeyframeRequestSource::kNewClient:
            return "new_client";
        case KeyframeRequestSource::kPacketLoss:
            return "packet_loss";
        case KeyframeRequestSource::kRecovery:
            return "recovery";
    }
    return "unknown";
}

SubscriptionClose MediaStreams::Impl::CloseReasonFromReset(
    MediaStreamResetReason reason) {
    switch (reason) {
        case MediaStreamResetReason::kCodecChanged:
            return SubscriptionClose::kCodecChanged;
        case MediaStreamResetReason::kTimestampReset:
            return SubscriptionClose::kTimestampReset;
        case MediaStreamResetReason::kCacheOverflow:
            return SubscriptionClose::kCacheOverflow;
        case MediaStreamResetReason::kStreamStarted:
        case MediaStreamResetReason::kStreamStopped:
            return SubscriptionClose::kStreamStopped;
        case MediaStreamResetReason::kNone:
            return SubscriptionClose::kNone;
    }
    return SubscriptionClose::kStreamStopped;
}

bool MediaStreams::Impl::Start() {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    if (phase_ == MediaStreamsPhase::kRunning) {
        return true;
    }
    if (phase_ == MediaStreamsPhase::kStopping) {
        Warn(kLogModuleName, "Start media streams rejected: state=%s",
             PhaseName(phase_));
        return false;
    }
    if (!ValidateOptions()) {
        return false;
    }

    ConfigureMediaCachesLocked();
    ResetStreamsLocked();
    phase_ = MediaStreamsPhase::kRunning;
    Info(kLogModuleName,
         "Media streams started hls_segment_ms=%u hls_playlist=%u "
         "hls_retain=%u max_flv=%u max_mjpeg=%u max_subscriptions=%u "
         "gop_frames=%u gop_bytes=%u shared_frames=%u shared_bytes=%u "
         "flv_tags=%u flv_bytes=%u hls_segments=%u hls_segment_bytes=%u "
         "hls_cached_bytes=%u",
         options_.hls_segment_duration_ms, options_.hls_playlist_depth,
         options_.hls_segment_retain_size, options_.max_flv_clients,
         options_.max_mjpeg_clients, options_.max_frame_subscriptions,
         options_.cache_limits.max_subscription_gop_frames,
         options_.cache_limits.max_subscription_gop_bytes,
         options_.cache_limits.max_shared_frames,
         options_.cache_limits.max_shared_bytes,
         options_.cache_limits.max_flv_cached_tags,
         options_.cache_limits.max_flv_cached_bytes,
         BuildStreamCacheOptions(options_).hls_maker.max_segments,
         options_.cache_limits.max_hls_segment_bytes,
         options_.cache_limits.max_hls_cached_bytes);
    return true;
}

void MediaStreams::Impl::Stop() {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    if (phase_ == MediaStreamsPhase::kStopped) {
        return;
    }
    phase_ = MediaStreamsPhase::kStopping;
    ResetStreamsLocked();
    phase_ = MediaStreamsPhase::kStopped;
    Info(kLogModuleName, "Media streams stopped");
}

bool MediaStreams::Impl::PushFrame(const MediaFrame &input_frame) {
    if (!IsMediaFramePayloadValid(input_frame)) {
        Warn(kLogModuleName,
             "Drop media frame: stream=%s codec=%s reason=invalid_payload "
             "bytes=%zu",
             StreamIdName(input_frame.stream_id), CodecName(input_frame.codec),
             input_frame.payload.Size());
        return false;
    }
    if (!IsStreamSupported(input_frame.stream_id)) {
        Warn(kLogModuleName,
             "Drop media frame: stream=%s codec=%s reason=invalid_stream",
             StreamIdName(input_frame.stream_id), CodecName(input_frame.codec));
        return false;
    }

    MediaFrame frame = input_frame;
    media_internal::ParsedFramePayload parsed_payload;
    if (!AcceptFrame(frame, parsed_payload)) {
        return false;
    }

    CachePreviewFrame(parsed_payload,
                        media_internal::IsFramePayloadParsed(parsed_payload));
    return true;
}

void MediaStreams::Impl::SetStreamState(StreamId stream_id,
                                        MediaStreamState state,
                                        Codec codec) {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    if (!IsStreamSupported(stream_id)) {
        Warn(kLogModuleName,
             "Set media stream state rejected: stream=%s reason=invalid_stream",
             StreamIdName(stream_id));
        return;
    }
    if (state == MediaStreamState::kRunning && !IsRunningLocked()) {
        Warn(kLogModuleName,
             "Set media stream state rejected: stream=%s state=%s reason=%s",
             StreamIdName(stream_id), MediaStreamStateName(state),
             PhaseName(phase_));
        return;
    }
    SetStreamStateLocked(stream_id, state, codec);
}

bool MediaStreams::Impl::IsHlsSupported(StreamId stream_id) const {
    return ReadStream(
        stream_id, false,
        [](const media_internal::StreamTrack &stream) {
            return media_internal::IsHlsStreamReady(stream);
        });
}

bool MediaStreams::Impl::IsFlvSupported(StreamId stream_id) const {
    return ReadStream(
        stream_id, false,
        [](const media_internal::StreamTrack &stream) {
            return media_internal::IsFlvStreamReady(stream);
        });
}

bool MediaStreams::Impl::IsMjpegSupported(StreamId stream_id) const {
    return ReadStream(
        stream_id, false,
        [](const media_internal::StreamTrack &stream) {
            return media_internal::IsMjpegStreamReady(stream);
        });
}

bool MediaStreams::Impl::IsStreamAvailable(StreamId stream_id) const {
    return ReadStream(
        stream_id, false,
        [](const media_internal::StreamTrack &stream) {
            return stream.state == MediaStreamState::kRunning;
        });
}

Codec MediaStreams::Impl::GetStreamCodec(StreamId stream_id) const {
    return ReadStream(stream_id, Codec::kH264,
                      [](const media_internal::StreamTrack &stream) {
                          return stream.codec;
                      });
}

MediaHlsPlaylist MediaStreams::Impl::GetHlsPlaylist(
    StreamId stream_id) const {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    const media_internal::StreamTrack *stream = FindStream(stream_id);
    if (stream == nullptr) {
        return MediaHlsPlaylist{};
    }
    stream->hls_maker.MarkRequested();
    return media_internal::BuildHlsPlaylist(
        *stream, options_.hls_segment_duration_ms,
        options_.hls_playlist_depth);
}

MediaSegmentRef MediaStreams::Impl::GetHlsSegmentRef(
    StreamId stream_id,
    uint64_t sequence) const {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    const media_internal::StreamTrack *stream = FindStream(stream_id);
    if (stream == nullptr) {
        return MediaSegmentRef{};
    }
    stream->hls_maker.MarkRequested();
    return media_internal::FindHlsSegmentRef(*stream, sequence);
}

MediaFlvStart MediaStreams::Impl::GetFlvStart(StreamId stream_id) const {
    return ReadStream(stream_id, MediaFlvStart{},
                      [](const media_internal::StreamTrack &stream) {
                          return media_internal::BuildFlvStart(stream);
                      });
}

MediaStreamInfo MediaStreams::Impl::GetStreamInfo(StreamId stream_id) const {
    return ReadStream(stream_id, MediaStreamInfo{},
                      [](const media_internal::StreamTrack &stream) {
                          return media_internal::BuildMediaStreamInfo(stream);
                      });
}

MediaStreamStats MediaStreams::Impl::GetStreamStats() const {
    std::shared_lock<std::shared_mutex> guard(mutex_);
    MediaStreamStats stats = stats_;
    stats.enabled = IsRunningLocked();
    stats.active_flv_clients =
        static_cast<uint32_t>(preview_clients_.FlvSize());
    stats.active_mjpeg_clients =
        static_cast<uint32_t>(preview_clients_.MjpegSize());
    stats.active_subscriptions =
        static_cast<uint32_t>(frame_subscribers_.SubscriberSize());
    stats.cached_frames = frame_subscribers_.CachedFrameSize();
    stats.main_cached_bytes = frame_subscribers_.CachedBytes(StreamId::kMain);
    stats.sub_cached_bytes = frame_subscribers_.CachedBytes(StreamId::kSub);
    stats.main_hls_cached_bytes =
        streams_.main_stream().hls_maker.CachedBytes();
    stats.sub_hls_cached_bytes =
        streams_.sub_stream().hls_maker.CachedBytes();
    stats.main_flv_cached_bytes = streams_.main_stream().flv_gop_cache.bytes();
    stats.sub_flv_cached_bytes = streams_.sub_stream().flv_gop_cache.bytes();
    stats.cached_bytes = 0;
    stats.cached_bytes =
        AddCachedBytes(stats.cached_bytes, stats.main_cached_bytes);
    stats.cached_bytes =
        AddCachedBytes(stats.cached_bytes, stats.sub_cached_bytes);
    stats.cached_bytes =
        AddCachedBytes(stats.cached_bytes, stats.main_hls_cached_bytes);
    stats.cached_bytes =
        AddCachedBytes(stats.cached_bytes, stats.sub_hls_cached_bytes);
    stats.cached_bytes =
        AddCachedBytes(stats.cached_bytes, stats.main_flv_cached_bytes);
    stats.cached_bytes =
        AddCachedBytes(stats.cached_bytes, stats.sub_flv_cached_bytes);
    stats.main_cache_drops =
        frame_subscribers_.CacheDropSize(StreamId::kMain) +
        streams_.main_stream().hls_maker.DropSize() +
        streams_.main_stream().flv_gop_cache.drop_size();
    stats.sub_cache_drops =
        frame_subscribers_.CacheDropSize(StreamId::kSub) +
        streams_.sub_stream().hls_maker.DropSize() +
        streams_.sub_stream().flv_gop_cache.drop_size();
    stats.main_client_frame_drops =
        frame_subscribers_.SubscriberDropSize(StreamId::kMain);
    stats.sub_client_frame_drops =
        frame_subscribers_.SubscriberDropSize(StreamId::kSub);
    stats.slow_subscriptions = frame_subscribers_.SlowSubscriberSize();
    stats.main_slow_subscriptions =
        frame_subscribers_.SlowSubscriberSize(StreamId::kMain);
    stats.sub_slow_subscriptions =
        frame_subscribers_.SlowSubscriberSize(StreamId::kSub);
    stats.main_last_frame_timestamp_us =
        frame_subscribers_.LastFrameTimestamp(StreamId::kMain);
    stats.sub_last_frame_timestamp_us =
        frame_subscribers_.LastFrameTimestamp(StreamId::kSub);
    stats.main_codec_generation = streams_.main_stream().codec_generation;
    stats.sub_codec_generation = streams_.sub_stream().codec_generation;
    stats.main_last_reset_reason =
        MediaStreamResetReasonName(streams_.main_stream().last_reset_reason);
    stats.sub_last_reset_reason =
        MediaStreamResetReasonName(streams_.sub_stream().last_reset_reason);
    return stats;
}

bool MediaStreams::Impl::RequestKeyframe(StreamId stream_id,
                                         KeyframeRequestSource source) {
    RequestKeyframeFn request_keyframe = nullptr;
    void *request_keyframe_user = nullptr;
    {
        std::shared_lock<std::shared_mutex> guard(mutex_);
        if (!IsStreamSupported(stream_id)) {
            Warn(kLogModuleName,
                 "Request keyframe rejected: stream=%s source=%s "
                 "reason=invalid_stream",
                 StreamIdName(stream_id), KeyframeRequestSourceName(source));
            return false;
        }
        if (!IsRunningLocked()) {
            Warn(kLogModuleName,
                 "Request keyframe rejected: stream=%s source=%s reason=%s",
                 StreamIdName(stream_id), KeyframeRequestSourceName(source),
                 PhaseName(phase_));
            return false;
        }
        if (options_.request_keyframe == nullptr) {
            Warn(kLogModuleName,
                 "Request keyframe rejected: stream=%s source=%s "
                 "reason=no_callback",
                 StreamIdName(stream_id), KeyframeRequestSourceName(source));
            return false;
        }
        request_keyframe = options_.request_keyframe;
        request_keyframe_user = options_.request_keyframe_user;
    }

    const bool requested =
        request_keyframe(stream_id, source, request_keyframe_user);
    if (!requested) {
        Warn(kLogModuleName,
             "Request keyframe failed: stream=%s source=%s reason=callback",
             StreamIdName(stream_id), KeyframeRequestSourceName(source));
    }
    return requested;
}

MediaFlvClientId MediaStreams::Impl::AttachFlvClient(
    StreamId stream_id,
    uint64_t config_generation,
    bool wait_for_keyframe,
    IMediaFlvSink *sink) {
    if (sink == nullptr) {
        Warn(kLogModuleName,
             "Attach FLV client rejected: stream=%s reason=null_sink",
             StreamIdName(stream_id));
        return 0;
    }

    MediaFlvClientId client_id = 0;
    {
        std::unique_lock<std::shared_mutex> guard(mutex_);
        if (!IsRunningLocked()) {
            Warn(kLogModuleName,
                 "Attach FLV client rejected: stream=%s reason=%s",
                 StreamIdName(stream_id), PhaseName(phase_));
            return 0;
        }
        if (!IsStreamSupported(stream_id)) {
            Warn(kLogModuleName,
                 "Attach FLV client rejected: stream=%s reason=invalid_stream",
                 StreamIdName(stream_id));
            return 0;
        }
        media_internal::StreamTrack &stream = MutableStreamTrackFor(stream_id);
        if (!media_internal::IsFlvStreamReady(stream)) {
            Warn(kLogModuleName,
                 "Attach FLV client rejected: stream=%s reason=not_ready "
                 "codec=%s stream_state=%s sequence_header=%d",
                 StreamIdName(stream_id), CodecName(stream.codec),
                 MediaStreamStateName(stream.state),
                 media_internal::IsFlvSequenceHeaderReady(stream) ? 1 : 0);
            return 0;
        }
        if (preview_clients_.FlvSize() >= options_.max_flv_clients) {
            Warn(kLogModuleName,
                 "Attach FLV client rejected: stream=%s reason=capacity "
                 "clients=%zu limit=%u",
                 StreamIdName(stream_id), preview_clients_.FlvSize(),
                 options_.max_flv_clients);
            return 0;
        }
        client_id = preview_clients_.AttachFlv(
            stream_id, config_generation, wait_for_keyframe, sink,
            options_.max_flv_clients);
        if (client_id == 0) {
            Warn(kLogModuleName,
                 "Attach FLV client rejected: stream=%s reason=attach_failed",
                 StreamIdName(stream_id));
            return 0;
        }
    }

    (void)RequestKeyframe(stream_id, KeyframeRequestSource::kNewClient);
    return client_id;
}

bool MediaStreams::Impl::DetachFlvClient(MediaFlvClientId client_id) {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    return preview_clients_.DetachFlv(client_id);
}

MediaMjpegClientId MediaStreams::Impl::AttachMjpegClient(
    StreamId stream_id,
    IMediaMjpegSink *sink) {
    if (sink == nullptr) {
        Warn(kLogModuleName,
             "Attach MJPEG client rejected: stream=%s reason=null_sink",
             StreamIdName(stream_id));
        return 0;
    }

    std::unique_lock<std::shared_mutex> guard(mutex_);
    if (!IsRunningLocked()) {
        Warn(kLogModuleName,
             "Attach MJPEG client rejected: stream=%s reason=%s",
             StreamIdName(stream_id), PhaseName(phase_));
        return 0;
    }
    if (!IsStreamSupported(stream_id)) {
        Warn(kLogModuleName,
             "Attach MJPEG client rejected: stream=%s reason=invalid_stream",
             StreamIdName(stream_id));
        return 0;
    }
    media_internal::StreamTrack &stream = MutableStreamTrackFor(stream_id);
    if (!media_internal::IsMjpegStreamReady(stream)) {
        Warn(kLogModuleName,
             "Attach MJPEG client rejected: stream=%s reason=not_ready "
             "codec=%s stream_state=%s latest=%d",
             StreamIdName(stream_id), CodecName(stream.codec),
             MediaStreamStateName(stream.state),
             stream.has_latest_mjpeg_frame ? 1 : 0);
        return 0;
    }
    if (preview_clients_.MjpegSize() >= options_.max_mjpeg_clients) {
        Warn(kLogModuleName,
             "Attach MJPEG client rejected: stream=%s reason=capacity "
             "clients=%zu limit=%u",
             StreamIdName(stream_id), preview_clients_.MjpegSize(),
             options_.max_mjpeg_clients);
        return 0;
    }
    const MediaMjpegClientId client_id =
        preview_clients_.AttachMjpeg(stream_id, sink,
                                     options_.max_mjpeg_clients);
    if (client_id == 0) {
        Warn(kLogModuleName,
             "Attach MJPEG client rejected: stream=%s reason=attach_failed",
             StreamIdName(stream_id));
    }
    return client_id;
}

bool MediaStreams::Impl::DetachMjpegClient(MediaMjpegClientId client_id) {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    return preview_clients_.DetachMjpeg(client_id);
}

FrameSubscriptionId MediaStreams::Impl::SubscribeFrames(
    const SubscriptionOptions &options) {
    if (!IsStreamSupported(options.stream_id)) {
        Warn(kLogModuleName,
             "Subscribe frames rejected: stream=%s reason=invalid_stream",
             StreamIdName(options.stream_id));
        return 0;
    }

    FrameSubscriptionId subscription_id = 0;
    {
        std::unique_lock<std::shared_mutex> guard(mutex_);
        if (!IsRunningLocked()) {
            Warn(kLogModuleName,
                 "Subscribe frames rejected: stream=%s reason=%s",
                 StreamIdName(options.stream_id), PhaseName(phase_));
            return 0;
        }
        if (frame_subscribers_.SubscriberSize() >=
            options_.max_frame_subscriptions) {
            Warn(kLogModuleName,
                 "Subscribe frames rejected: stream=%s reason=capacity "
                 "subscriptions=%zu limit=%u",
                 StreamIdName(options.stream_id),
                 frame_subscribers_.SubscriberSize(),
                 options_.max_frame_subscriptions);
            return 0;
        }
        subscription_id = frame_subscribers_.SubscribeFrames(
            options, options_.max_frame_subscriptions);
        if (subscription_id == 0) {
            Warn(kLogModuleName,
                 "Subscribe frames rejected: stream=%s reason=attach_failed",
                 StreamIdName(options.stream_id));
            return 0;
        }
    }
    if (options.keyframe_first) {
        (void)RequestKeyframe(options.stream_id,
                              KeyframeRequestSource::kNewClient);
    }
    return subscription_id;
}

bool MediaStreams::Impl::UnsubscribeFrames(
    FrameSubscriptionId subscription_id,
    SubscriptionClose reason) {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    return frame_subscribers_.UnsubscribeFrames(subscription_id, reason);
}

SubscriptionInfo MediaStreams::Impl::GetSubscriptionInfo(
    FrameSubscriptionId subscription_id) const {
    std::shared_lock<std::shared_mutex> guard(mutex_);
    return frame_subscribers_.GetSubscriptionInfo(subscription_id);
}

SubscriptionStart MediaStreams::Impl::GetSubscriptionStart(
    FrameSubscriptionId subscription_id) const {
    std::shared_lock<std::shared_mutex> guard(mutex_);
    const SubscriptionInfo subscription_info =
        frame_subscribers_.GetSubscriptionInfo(subscription_id);
    if (!subscription_info.open) {
        return SubscriptionStart{};
    }
    const media_internal::StreamTrack *stream =
        FindStream(subscription_info.stream_id);
    const MediaStreamInfo stream_info =
        media_internal::BuildMediaStreamInfo(*stream);
    return frame_subscribers_.GetSubscriptionStart(subscription_id, stream_info);
}

bool MediaStreams::Impl::PullFrame(
    FrameSubscriptionId subscription_id,
    SubscriptionFrame *frame) {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    return frame_subscribers_.PullFrame(subscription_id, frame);
}

bool MediaStreams::Impl::ValidateOptions() const {
    if (options_.hls_segment_duration_ms == 0) {
        Error(kLogModuleName,
              "Start media streams failed: hls_segment_duration_ms=0");
        return false;
    }
    if (options_.hls_playlist_depth == 0) {
        Error(kLogModuleName,
              "Start media streams failed: hls_playlist_depth=0");
        return false;
    }
    if (options_.hls_segment_retain_size >
        std::numeric_limits<uint32_t>::max() -
            options_.hls_playlist_depth) {
        Error(kLogModuleName,
              "Start media streams failed: hls cache depth overflow "
              "playlist=%u retain=%u",
              options_.hls_playlist_depth,
              options_.hls_segment_retain_size);
        return false;
    }
    if (options_.max_flv_clients == 0) {
        Error(kLogModuleName, "Start media streams failed: max_flv_clients=0");
        return false;
    }
    if (options_.max_mjpeg_clients == 0) {
        Error(kLogModuleName,
              "Start media streams failed: max_mjpeg_clients=0");
        return false;
    }
    if (options_.max_frame_subscriptions == 0) {
        Error(kLogModuleName,
              "Start media streams failed: max_frame_subscriptions=0");
        return false;
    }
    if (!ValidateCacheLimits()) {
        return false;
    }
    return true;
}

bool MediaStreams::Impl::ValidateCacheLimits() const {
    const MediaCacheLimits &limits = options_.cache_limits;
    if (limits.max_subscription_gop_frames == 0) {
        Error(kLogModuleName,
              "Start media streams failed: max_subscription_gop_frames=0");
        return false;
    }
    if (limits.max_subscription_gop_bytes == 0) {
        Error(kLogModuleName,
              "Start media streams failed: max_subscription_gop_bytes=0");
        return false;
    }
    if (limits.max_shared_frames == 0) {
        Error(kLogModuleName,
              "Start media streams failed: max_shared_frames=0");
        return false;
    }
    if (limits.max_shared_bytes == 0) {
        Error(kLogModuleName,
              "Start media streams failed: max_shared_bytes=0");
        return false;
    }
    if (limits.max_flv_cached_tags == 0) {
        Error(kLogModuleName,
              "Start media streams failed: max_flv_cached_tags=0");
        return false;
    }
    if (limits.max_flv_cached_bytes == 0) {
        Error(kLogModuleName,
              "Start media streams failed: max_flv_cached_bytes=0");
        return false;
    }
    if (limits.max_hls_segments == 0) {
        Error(kLogModuleName,
              "Start media streams failed: max_hls_segments=0");
        return false;
    }
    if (limits.max_hls_segment_bytes == 0) {
        Error(kLogModuleName,
              "Start media streams failed: max_hls_segment_bytes=0");
        return false;
    }
    if (limits.max_hls_segment_bytes < kMinHlsSegmentBytes) {
        Error(kLogModuleName,
              "Start media streams failed: max_hls_segment_bytes=%u "
              "min=%u",
              limits.max_hls_segment_bytes, kMinHlsSegmentBytes);
        return false;
    }
    if (limits.max_hls_cached_bytes < limits.max_hls_segment_bytes) {
        Error(kLogModuleName,
              "Start media streams failed: hls_cached_bytes=%u "
              "hls_segment_bytes=%u",
              limits.max_hls_cached_bytes, limits.max_hls_segment_bytes);
        return false;
    }
    return true;
}

bool MediaStreams::Impl::IsRunningLocked() const {
    return phase_ == MediaStreamsPhase::kRunning;
}

void MediaStreams::Impl::ConfigureMediaCachesLocked() {
    frame_subscribers_.Configure(
        BuildFrameSubscribersOptions(options_.cache_limits));
    streams_.Configure(BuildStreamCacheOptions(options_));
}

void MediaStreams::Impl::ResetStreamsLocked() {
    frame_subscribers_.Clear();
    preview_clients_.Clear();
    streams_.Clear();
    stats_ = MediaStreamStats{};
}

void MediaStreams::Impl::ApplyResetNoticeLocked(
    const media_internal::StreamResetNotice &notice) {
    if (!notice.reset) {
        return;
    }
    frame_subscribers_.ClearStream(notice.stream_id,
                                   CloseReasonFromReset(notice.reason));
    Info(kLogModuleName, "Media stream reset stream=%s codec=%s reason=%s",
         StreamIdName(notice.stream_id), CodecName(notice.codec),
         MediaStreamResetReasonName(notice.reason));
}

void MediaStreams::Impl::EnsureRunningStreamLocked(
    media_internal::StreamTrack &stream,
    StreamId stream_id,
    Codec codec) {
    const MediaStreamState prev_stream_state = stream.state;
    const Codec codec_before = stream.codec;
    media_internal::StreamResetNotice notice;
    streams_.EnsureRunning(stream_id, codec, notice);
    ApplyResetNoticeLocked(notice);
    if (prev_stream_state != MediaStreamState::kRunning) {
        Info(kLogModuleName, "Media stream started stream=%s codec=%s",
             StreamIdName(stream_id), CodecName(codec));
        return;
    }
    if (codec_before != codec) {
        Info(kLogModuleName, "Media stream codec changed stream=%s codec=%s",
             StreamIdName(stream_id), CodecName(codec));
    }
}

bool MediaStreams::Impl::AcceptFrame(
    MediaFrame &frame,
    media_internal::ParsedFramePayload &parsed_payload) {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    if (!IsRunningLocked()) {
        Warn(kLogModuleName,
             "Drop media frame: stream=%s codec=%s reason=%s",
             StreamIdName(frame.stream_id), CodecName(frame.codec),
             PhaseName(phase_));
        return false;
    }
    media_internal::StreamTrack &stream = MutableStreamTrackFor(frame.stream_id);
    EnsureRunningStreamLocked(stream, frame.stream_id, frame.codec);
    if (!NormalizeFrameTimestampLocked(stream, frame)) {
        return false;
    }
    parsed_payload = ParseFramePayloadView(frame);
    if (stream.state == MediaStreamState::kRunning) {
        frame_subscribers_.Write(parsed_payload);
    }
    return true;
}

bool MediaStreams::Impl::NormalizeFrameTimestampLocked(
    media_internal::StreamTrack &stream,
    MediaFrame &frame) {
    const media_internal::NormalizedFrameResult normalized_frame =
        media_internal::NormalizeFrameTimestamps(stream, frame);
    if (normalized_frame.timestamp_reset) {
        frame_subscribers_.ClearStream(frame.stream_id,
                                       SubscriptionClose::kTimestampReset);
        Warn(kLogModuleName,
             "Media stream timestamp reset stream=%s codec=%s pts_us=%lld "
             "dts_us=%lld",
             StreamIdName(frame.stream_id), CodecName(frame.codec),
             static_cast<long long>(frame.pts_us),
             static_cast<long long>(frame.dts_us));
    }
    if (!normalized_frame.accepted) {
        Warn(kLogModuleName,
             "Drop media frame: stream=%s codec=%s reason=timestamp_rejected",
             StreamIdName(frame.stream_id), CodecName(frame.codec));
    }
    return normalized_frame.accepted;
}

media_internal::ParsedFramePayload
MediaStreams::Impl::ParseFramePayloadView(
    const MediaFrame &frame) const {
    media_internal::ParsedFramePayload parsed_payload;
    media_internal::ParseFramePayload(frame, parsed_payload);
    return parsed_payload;
}

void MediaStreams::Impl::CachePreviewFrame(
    const media_internal::ParsedFramePayload &parsed_payload,
    bool has_payload) {
    if (!has_payload) {
        PackageMjpegFrame(parsed_payload);
        return;
    }

    const MediaFrame &frame = parsed_payload.frame;
    std::string sequence_header_tag;
    media_internal::FlvVideoTagBuild flv_tag_view;
    bool has_flv_tag_view = false;
    bool keyframe = false;
    uint64_t config_generation = 0;
    {
        std::unique_lock<std::shared_mutex> guard(mutex_);
        if (!IsRunningLocked()) {
            return;
        }
        media_internal::StreamTrack &stream =
            MutableStreamTrackFor(frame.stream_id);
        EnsureRunningStreamLocked(stream, frame.stream_id, frame.codec);
        if (!media_internal::IsPreviewStreamReady(stream.state,
                                                  stream.codec)) {
            return;
        }
        const bool package_hls =
            media_internal::IsHlsCodecSupported(stream.codec);
        const bool package_flv =
            preview_clients_.HasFlvClient(frame.stream_id);
        const bool update_flv_cache =
            media_internal::IsFlvCodecSupported(stream.codec);
        if (!package_hls && !package_flv && !update_flv_cache) {
            return;
        }

        media_internal::PackagedFrameResult packaged_frame =
            media_internal::AppendFrameToStream(
                stream, frame, parsed_payload, package_hls,
                package_flv || update_flv_cache,
                options_.hls_segment_duration_ms, flv_tag_view);
        if (!packaged_frame.accepted) {
            return;
        }
        if (packaged_frame.hls_segment_created) {
            ++stats_.hls_segments_created;
        }
        has_flv_tag_view = packaged_frame.has_flv_tag_view;
        keyframe = packaged_frame.keyframe;
        config_generation = stream.config_generation;
        sequence_header_tag = stream.sequence_header_tag;
    }
    preview_clients_.WriteFlv(frame.stream_id, config_generation, keyframe,
                              sequence_header_tag, flv_tag_view,
                              has_flv_tag_view, frame);
}

void MediaStreams::Impl::PackageMjpegFrame(
    const media_internal::ParsedFramePayload &parsed_payload) {
    const MediaFrame &frame = parsed_payload.frame;
    if (frame.codec != Codec::kMjpeg ||
        !IsMediaFramePayloadValid(frame)) {
        return;
    }

    bool write_mjpeg = false;
    {
        std::unique_lock<std::shared_mutex> guard(mutex_);
        if (!IsRunningLocked()) {
            return;
        }
        media_internal::StreamTrack &stream =
            MutableStreamTrackFor(frame.stream_id);
        EnsureRunningStreamLocked(stream, frame.stream_id, frame.codec);
        if (!media_internal::CacheMjpegFrame(stream, frame)) {
            return;
        }
        write_mjpeg = preview_clients_.HasMjpegClient(frame.stream_id);
    }
    if (write_mjpeg) {
        preview_clients_.WriteMjpeg(frame.stream_id, frame);
    }
}

const media_internal::StreamTrack *MediaStreams::Impl::FindStream(
    StreamId stream_id) const {
    return streams_.Find(stream_id);
}

media_internal::StreamTrack &MediaStreams::Impl::MutableStreamTrackFor(
    StreamId stream_id) {
    return streams_.Mutable(stream_id);
}

void MediaStreams::Impl::SetStreamStateLocked(StreamId stream_id,
                                              MediaStreamState state,
                                              Codec codec) {
    media_internal::StreamTrack &stream = MutableStreamTrackFor(stream_id);
    const MediaStreamState prev_stream_state = stream.state;
    media_internal::StreamResetNotice notice;
    streams_.SetState(stream_id, state, codec, notice);
    ApplyResetNoticeLocked(notice);
    if (prev_stream_state != state) {
        Info(kLogModuleName, "Media stream state changed stream=%s state=%s",
             StreamIdName(stream_id), MediaStreamStateName(state));
    }
}

}  // namespace live_stream
