#include "media_streams_impl.h"

#include "infra/log.h"

#include <limits>
#include <utility>

namespace live_stream {

MediaStreams::Impl::Impl(MediaStreamsOptions options)
    : options_(std::move(options)) {}

MediaStreams::Impl::~Impl() { Stop(); }

uint32_t MediaStreams::Impl::HlsSegmentCacheDepth(
    const MediaStreamsOptions &options) {
    return options.hls_playlist_depth + options.hls_segment_retain_size;
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

const char *MediaStreams::Impl::RunStateName(
    MediaStreamsRunState state) {
    switch (state) {
        case MediaStreamsRunState::kStopped:
            return "stopped";
        case MediaStreamsRunState::kRunning:
            return "running";
        case MediaStreamsRunState::kStopping:
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

SubscriptionClose MediaStreams::Impl::CloseReasonForReset(
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
    if (run_state_ == MediaStreamsRunState::kRunning) {
        return true;
    }
    if (run_state_ == MediaStreamsRunState::kStopping) {
        Warn(kLogModuleName, "Start media streams rejected: state=%s",
             RunStateName(run_state_));
        return false;
    }
    if (!ValidateOptions()) {
        return false;
    }

    ResetMediaStateLocked();
    run_state_ = MediaStreamsRunState::kRunning;
    Info(kLogModuleName,
         "Media streams started hls_segment_ms=%u hls_playlist=%u "
         "hls_retain=%u max_flv=%u max_mjpeg=%u max_subscriptions=%u",
         options_.hls_segment_duration_ms, options_.hls_playlist_depth,
         options_.hls_segment_retain_size, options_.max_flv_clients,
         options_.max_mjpeg_clients, options_.max_frame_subscriptions);
    return true;
}

void MediaStreams::Impl::Stop() {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    if (run_state_ == MediaStreamsRunState::kStopped) {
        return;
    }
    run_state_ = MediaStreamsRunState::kStopping;
    ResetMediaStateLocked();
    run_state_ = MediaStreamsRunState::kStopped;
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
    if (!AcceptFrame(&frame)) {
        return false;
    }

    const media_internal::ParsedFramePayload payload =
        ParseFramePayloadView(frame);
    QueueFrameForSubscriptions(payload);
    PackagePreviewFrame(payload,
                        media_internal::IsFramePayloadParsed(payload));
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
             RunStateName(run_state_));
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
        static_cast<uint32_t>(frame_ring_.SubscriptionSize());
    stats.cached_frames = frame_ring_.CachedFrameSize();
    stats.cached_bytes = frame_ring_.CachedBytes();
    stats.slow_subscriptions = frame_ring_.SlowSubscriptionSize();
    stats.main_slow_subscriptions =
        frame_ring_.SlowSubscriptionSize(StreamId::kMain);
    stats.sub_slow_subscriptions =
        frame_ring_.SlowSubscriptionSize(StreamId::kSub);
    stats.main_last_frame_timestamp_us =
        frame_ring_.LastFrameTimestamp(StreamId::kMain);
    stats.sub_last_frame_timestamp_us =
        frame_ring_.LastFrameTimestamp(StreamId::kSub);
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
                 RunStateName(run_state_));
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
        media_internal::StreamTrack *stream = FindMutableStream(stream_id);
        if (!IsRunningLocked()) {
            Warn(kLogModuleName,
                 "Attach FLV client rejected: stream=%s reason=%s",
                 StreamIdName(stream_id), RunStateName(run_state_));
            return 0;
        }
        if (stream == nullptr) {
            Warn(kLogModuleName,
                 "Attach FLV client rejected: stream=%s reason=invalid_stream",
                 StreamIdName(stream_id));
            return 0;
        }
        if (!media_internal::IsFlvStreamReady(*stream)) {
            Warn(kLogModuleName,
                 "Attach FLV client rejected: stream=%s reason=not_ready "
                 "codec=%s stream_state=%s sequence_header=%d",
                 StreamIdName(stream_id), CodecName(stream->codec),
                 MediaStreamStateName(stream->state),
                 media_internal::IsFlvSequenceHeaderReady(*stream) ? 1 : 0);
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
    media_internal::StreamTrack *stream = FindMutableStream(stream_id);
    if (!IsRunningLocked()) {
        Warn(kLogModuleName,
             "Attach MJPEG client rejected: stream=%s reason=%s",
             StreamIdName(stream_id), RunStateName(run_state_));
        return 0;
    }
    if (stream == nullptr) {
        Warn(kLogModuleName,
             "Attach MJPEG client rejected: stream=%s reason=invalid_stream",
             StreamIdName(stream_id));
        return 0;
    }
    if (!media_internal::IsMjpegStreamReady(*stream)) {
        Warn(kLogModuleName,
             "Attach MJPEG client rejected: stream=%s reason=not_ready "
             "codec=%s stream_state=%s latest=%d",
             StreamIdName(stream_id), CodecName(stream->codec),
             MediaStreamStateName(stream->state),
             stream->has_latest_mjpeg_frame ? 1 : 0);
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
                 StreamIdName(options.stream_id), RunStateName(run_state_));
            return 0;
        }
        if (FindStream(options.stream_id) == nullptr) {
            Warn(kLogModuleName,
                 "Subscribe frames rejected: stream=%s reason=invalid_stream",
                 StreamIdName(options.stream_id));
            return 0;
        }
        if (frame_ring_.SubscriptionSize() >=
            options_.max_frame_subscriptions) {
            Warn(kLogModuleName,
                 "Subscribe frames rejected: stream=%s reason=capacity "
                 "subscriptions=%zu limit=%u",
                 StreamIdName(options.stream_id),
                 frame_ring_.SubscriptionSize(),
                 options_.max_frame_subscriptions);
            return 0;
        }
        subscription_id = frame_ring_.AttachSubscription(
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
    return frame_ring_.DetachSubscription(subscription_id, reason);
}

SubscriptionInfo MediaStreams::Impl::GetSubscriptionInfo(
    FrameSubscriptionId subscription_id) const {
    std::shared_lock<std::shared_mutex> guard(mutex_);
    return frame_ring_.GetSubscriptionInfo(subscription_id);
}

SubscriptionStart MediaStreams::Impl::GetSubscriptionStart(
    FrameSubscriptionId subscription_id) const {
    std::shared_lock<std::shared_mutex> guard(mutex_);
    const SubscriptionInfo subscription_info =
        frame_ring_.GetSubscriptionInfo(subscription_id);
    if (!subscription_info.open) {
        return SubscriptionStart{};
    }
    const media_internal::StreamTrack *stream =
        FindStream(subscription_info.stream_id);
    if (stream == nullptr) {
        return SubscriptionStart{};
    }
    const MediaStreamInfo stream_info =
        media_internal::BuildMediaStreamInfo(*stream);
    return frame_ring_.GetSubscriptionStart(subscription_id, stream_info);
}

bool MediaStreams::Impl::PopSubscriptionFrame(
    FrameSubscriptionId subscription_id,
    SubscriptionFrame *frame) {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    return frame_ring_.PopFrame(subscription_id, frame);
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
    return true;
}

bool MediaStreams::Impl::IsRunningLocked() const {
    return run_state_ == MediaStreamsRunState::kRunning;
}

void MediaStreams::Impl::ResetMediaStateLocked() {
    frame_ring_.Clear();
    preview_clients_.Clear();
    streams_.Clear();
    stats_ = MediaStreamStats{};
}

void MediaStreams::Impl::ApplyResetNoticeLocked(
    const media_internal::StreamResetNotice &notice) {
    if (!notice.reset) {
        return;
    }
    frame_ring_.ClearStream(notice.stream_id,
                            CloseReasonForReset(notice.reason));
    Info(kLogModuleName, "Media stream reset stream=%s codec=%s reason=%s",
         StreamIdName(notice.stream_id), CodecName(notice.codec),
         MediaStreamResetReasonName(notice.reason));
}

void MediaStreams::Impl::EnsureRunningStreamLocked(
    media_internal::StreamTrack *stream,
    StreamId stream_id,
    Codec codec) {
    if (stream == nullptr) {
        return;
    }
    const MediaStreamState state_before = stream->state;
    const Codec codec_before = stream->codec;
    media_internal::StreamResetNotice notice;
    if (!streams_.EnsureRunning(stream_id, codec, &notice)) {
        return;
    }
    ApplyResetNoticeLocked(notice);
    if (state_before != MediaStreamState::kRunning) {
        Info(kLogModuleName, "Media stream started stream=%s codec=%s",
             StreamIdName(stream_id), CodecName(codec));
        return;
    }
    if (codec_before != codec) {
        Info(kLogModuleName, "Media stream codec changed stream=%s codec=%s",
             StreamIdName(stream_id), CodecName(codec));
    }
}

bool MediaStreams::Impl::AcceptFrame(MediaFrame *frame) {
    if (frame == nullptr) {
        return false;
    }

    std::unique_lock<std::shared_mutex> guard(mutex_);
    if (!IsRunningLocked()) {
        Warn(kLogModuleName,
             "Drop media frame: stream=%s codec=%s reason=%s",
             StreamIdName(frame->stream_id), CodecName(frame->codec),
             RunStateName(run_state_));
        return false;
    }
    media_internal::StreamTrack *stream =
        FindMutableStream(frame->stream_id);
    if (stream == nullptr) {
        Warn(kLogModuleName,
             "Drop media frame: stream=%s codec=%s reason=invalid_stream",
             StreamIdName(frame->stream_id), CodecName(frame->codec));
        return false;
    }
    EnsureRunningStreamLocked(stream, frame->stream_id, frame->codec);
    return NormalizeFrameTimestampLocked(stream, frame);
}

bool MediaStreams::Impl::NormalizeFrameTimestampLocked(
    media_internal::StreamTrack *stream,
    MediaFrame *frame) {
    const media_internal::NormalizedFrameResult result =
        media_internal::NormalizeFrameTimestamps(stream, frame);
    if (result.timestamp_reset) {
        frame_ring_.ClearStream(frame->stream_id,
                                SubscriptionClose::kTimestampReset);
        Warn(kLogModuleName,
             "Media stream timestamp reset stream=%s codec=%s pts_us=%lld "
             "dts_us=%lld",
             StreamIdName(frame->stream_id), CodecName(frame->codec),
             static_cast<long long>(frame->pts_us),
             static_cast<long long>(frame->dts_us));
    }
    if (!result.accepted) {
        Warn(kLogModuleName,
             "Drop media frame: stream=%s codec=%s reason=timestamp_rejected",
             StreamIdName(frame->stream_id), CodecName(frame->codec));
    }
    return result.accepted;
}

media_internal::ParsedFramePayload
MediaStreams::Impl::ParseFramePayloadView(
    const MediaFrame &frame) const {
    media_internal::ParsedFramePayload payload;
    media_internal::ParseFramePayload(frame, &payload);
    return payload;
}

void MediaStreams::Impl::QueueFrameForSubscriptions(
    const media_internal::ParsedFramePayload &payload) {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    if (!IsRunningLocked()) {
        return;
    }
    media_internal::StreamTrack *stream =
        FindMutableStream(payload.frame.stream_id);
    if (stream == nullptr ||
        stream->state != MediaStreamState::kRunning) {
        return;
    }
    frame_ring_.Write(payload);
}

void MediaStreams::Impl::PackagePreviewFrame(
    const media_internal::ParsedFramePayload &payload,
    bool has_payload) {
    if (!has_payload) {
        PackageMjpegFrame(payload);
        return;
    }

    const MediaFrame &frame = payload.frame;
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
        media_internal::StreamTrack *stream =
            FindMutableStream(frame.stream_id);
        if (stream == nullptr) {
            return;
        }
        EnsureRunningStreamLocked(stream, frame.stream_id, frame.codec);
        if (!media_internal::IsPreviewStreamReady(stream->state,
                                                  stream->codec)) {
            return;
        }
        const bool package_hls =
            media_internal::IsHlsCodecSupported(stream->codec);
        const bool package_flv =
            preview_clients_.HasFlvClient(frame.stream_id);
        const bool update_flv_cache =
            media_internal::IsFlvCodecSupported(stream->codec);
        if (!package_hls && !package_flv && !update_flv_cache) {
            return;
        }

        media_internal::PackagedFrameResult packaged_frame =
            media_internal::AppendFrameToStream(
                stream, frame, payload, package_hls,
                package_flv || update_flv_cache,
                options_.hls_segment_duration_ms,
                HlsSegmentCacheDepth(options_));
        if (!packaged_frame.accepted) {
            return;
        }
        if (packaged_frame.hls_segment_created) {
            ++stats_.hls_segments_created;
        }
        flv_tag_view = packaged_frame.flv_tag_view;
        has_flv_tag_view = packaged_frame.has_flv_tag_view;
        keyframe = packaged_frame.keyframe;
        config_generation = stream->config_generation;
        sequence_header_tag = stream->sequence_header_tag;
    }
    preview_clients_.WriteFlv(frame.stream_id, config_generation, keyframe,
                              sequence_header_tag, flv_tag_view,
                              has_flv_tag_view, frame);
}

void MediaStreams::Impl::PackageMjpegFrame(
    const media_internal::ParsedFramePayload &payload) {
    const MediaFrame &frame = payload.frame;
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
        media_internal::StreamTrack *stream =
            FindMutableStream(frame.stream_id);
        if (stream == nullptr) {
            return;
        }
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

media_internal::StreamTrack *MediaStreams::Impl::FindMutableStream(
    StreamId stream_id) {
    return streams_.FindMutable(stream_id);
}

void MediaStreams::Impl::SetStreamStateLocked(StreamId stream_id,
                                              MediaStreamState state,
                                              Codec codec) {
    media_internal::StreamTrack *stream = FindMutableStream(stream_id);
    if (stream == nullptr) {
        return;
    }
    const MediaStreamState state_before = stream->state;
    media_internal::StreamResetNotice notice;
    streams_.SetState(stream_id, state, codec, &notice);
    ApplyResetNoticeLocked(notice);
    if (state_before != state) {
        Info(kLogModuleName, "Media stream state changed stream=%s state=%s",
             StreamIdName(stream_id), MediaStreamStateName(state));
    }
}

}  // namespace live_stream
