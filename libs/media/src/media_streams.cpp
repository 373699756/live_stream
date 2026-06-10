#include "media/media_streams.h"

#include "flv_live_ring.h"
#include "frame_ring.h"
#include "media_stream_state.h"
#include "media_codec.h"
#include "mjpeg_client_registry.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace live_stream {
namespace source_state = media_internal;
namespace source_clients = media_streams_internal;
namespace {

uint32_t HlsSegmentCacheDepth(const MediaStreamsOptions &options) {
    return options.hls_playlist_depth + options.hls_segment_retain_count;
}

bool IsStreamSupported(StreamId stream_id) {
    return stream_id == StreamId::kMain || stream_id == StreamId::kSub;
}

MediaFlvVideoTagView ToMediaFlvVideoTagView(
    const source_state::FlvVideoTagView &tag) {
    MediaFlvVideoTagView output_tag;
    if (tag.slice_count > kMaxMediaFlvVideoTagSlices) {
        return output_tag;
    }
    output_tag.slice_count = tag.slice_count;
    output_tag.timestamp_ms = tag.timestamp_ms;
    for (size_t i = 0; i < tag.slice_count; ++i) {
        output_tag.slices[i].data = tag.slices[i].data;
        output_tag.slices[i].size = tag.slices[i].size;
        output_tag.slices[i].media_payload = tag.slices[i].media_payload;
    }
    return output_tag;
}

FrameSubscriptionCloseReason CloseReasonForReset(
    MediaStreamResetReason reason) {
    switch (reason) {
        case MediaStreamResetReason::kCodecChanged:
            return FrameSubscriptionCloseReason::kCodecChanged;
        case MediaStreamResetReason::kTimestampReset:
            return FrameSubscriptionCloseReason::kTimestampReset;
        case MediaStreamResetReason::kCacheOverflow:
            return FrameSubscriptionCloseReason::kCacheOverflow;
        case MediaStreamResetReason::kStreamStarted:
        case MediaStreamResetReason::kStreamStopped:
            return FrameSubscriptionCloseReason::kStreamStopped;
        case MediaStreamResetReason::kNone:
            return FrameSubscriptionCloseReason::kNone;
    }
    return FrameSubscriptionCloseReason::kStreamStopped;
}

}  // namespace

const char *MediaStreamResetReasonName(MediaStreamResetReason reason) {
    switch (reason) {
        case MediaStreamResetReason::kNone:
            return "none";
        case MediaStreamResetReason::kStreamStarted:
            return "stream_started";
        case MediaStreamResetReason::kStreamStopped:
            return "stream_stopped";
        case MediaStreamResetReason::kCodecChanged:
            return "codec_changed";
        case MediaStreamResetReason::kTimestampReset:
            return "timestamp_reset";
        case MediaStreamResetReason::kCacheOverflow:
            return "cache_overflow";
    }
    return "unknown";
}

const char *FrameSubscriptionCloseReasonName(
    FrameSubscriptionCloseReason reason) {
    switch (reason) {
        case FrameSubscriptionCloseReason::kNone:
            return "none";
        case FrameSubscriptionCloseReason::kUnsubscribed:
            return "unsubscribed";
        case FrameSubscriptionCloseReason::kStreamStopped:
            return "stream_stopped";
        case FrameSubscriptionCloseReason::kCodecChanged:
            return "codec_changed";
        case FrameSubscriptionCloseReason::kTimestampReset:
            return "timestamp_reset";
        case FrameSubscriptionCloseReason::kCacheOverflow:
            return "cache_overflow";
    }
    return "unknown";
}

void MediaFlvCachedVideoTagUnref(MediaFlvCachedVideoTag *tag) {
    if (tag == nullptr) {
        return;
    }
    EncodedFrameUnref(&tag->frame);
    *tag = MediaFlvCachedVideoTag{};
}

bool MediaFlvCachedVideoTagRefCopy(MediaFlvCachedVideoTag *target,
                                   const MediaFlvCachedVideoTag *source) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    if (target == source) {
        return true;
    }
    EncodedFrame retained_frame;
    if (!EncodedFrameRefCopy(&retained_frame, &source->frame)) {
        return false;
    }
    MediaFlvCachedVideoTagUnref(target);
    *target = *source;
    target->frame = retained_frame;
    return true;
}

void MediaFlvStartDataUnref(MediaFlvStartData *start_data) {
    if (start_data == nullptr) {
        return;
    }
    for (MediaFlvCachedVideoTag &tag : start_data->cached_video_tags) {
        MediaFlvCachedVideoTagUnref(&tag);
    }
    start_data->cached_video_tags.clear();
    start_data->file_header.clear();
    start_data->sequence_header.clear();
    start_data->supported = false;
    start_data->cached_gop_complete = false;
    start_data->config_generation = 0;
}

void FrameSubscriptionStartDataUnref(
    FrameSubscriptionStartData *start_data) {
    if (start_data == nullptr) {
        return;
    }
    for (MediaFrame &frame : start_data->gop_frames) {
        MediaFrameUnref(&frame);
    }
    start_data->gop_frames.clear();
    start_data->stream_running = false;
    start_data->gop_complete = false;
    start_data->subscription_generation = 0;
    start_data->track = MediaTrack{};
}

void SubscribedFrameUnref(SubscribedFrame *subscribed_frame) {
    if (subscribed_frame == nullptr) {
        return;
    }
    MediaFrameUnref(&subscribed_frame->frame);
    subscribed_frame->subscription_id = 0;
    subscribed_frame->subscription_generation = 0;
    subscribed_frame->starts_on_keyframe = false;
}

MediaSegmentRef MediaSegmentRefCopy(const MediaSegmentRef *segment) {
    MediaSegmentRef ref;
    if (segment == nullptr || !segment->found || segment->body == nullptr) {
        return ref;
    }
    ref = *segment;
    ref.body = FrameBufferRef(segment->body);
    if (ref.body == nullptr) {
        return MediaSegmentRef{};
    }
    return ref;
}

void MediaSegmentRefUnref(MediaSegmentRef *segment) {
    if (segment == nullptr) {
        return;
    }
    FrameBufferUnref(segment->body);
    *segment = MediaSegmentRef{};
}

class MediaStreams::Impl {
public:
    explicit Impl(MediaStreamsOptions options) : options_(std::move(options)) {}

    ~Impl() { Stop(); }

    bool Start() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (started_) {
            return true;
        }
        if (!ValidateOptions()) {
            return false;
        }
        ResetRuntimeStateLocked();
        started_ = true;
        return true;
    }

    void Stop() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!started_) {
            return;
        }
        ResetRuntimeStateLocked();
        started_ = false;
    }

    bool PushFrame(const EncodedFrame &input_frame) {
        if (!EncodedFrameHasPayload(&input_frame) ||
            !IsStreamSupported(input_frame.stream_id)) {
            return false;
        }

        EncodedFrame frame;
        if (!EncodedFrameRefCopy(&frame, &input_frame)) {
            return false;
        }

        bool normalized = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!started_) {
                EncodedFrameUnref(&frame);
                return false;
            }
            source_state::StreamContext *stream =
                FindMutableStream(frame.stream_id);
            if (stream == nullptr) {
                EncodedFrameUnref(&frame);
                return false;
            }
            EnsureRunningStreamLocked(stream, frame.stream_id, frame.codec);
            const source_state::NormalizedFrameResult result =
                source_state::NormalizeFrameTimestamps(stream, &frame);
            if (result.timestamp_reset) {
                frame_ring_.ClearStream(
                    frame.stream_id,
                    FrameSubscriptionCloseReason::kTimestampReset);
            }
            normalized = result.accepted;
        }

        if (!normalized) {
            EncodedFrameUnref(&frame);
            return false;
        }

        source_state::ParsedFramePayload payload;
        source_state::ParseFramePayload(frame, &payload);
        QueueFrameForSubscribers(payload);
        PackageBrowserFrame(payload, source_state::HasParsedUnits(payload));
        source_state::ParsedFramePayloadUnref(&payload);
        EncodedFrameUnref(&frame);
        return true;
    }

    void SetStreamState(StreamId stream_id, MediaStreamState state,
                        Codec codec) {
        std::lock_guard<std::mutex> guard(mutex_);
        SetStreamStateLocked(stream_id, state, codec);
    }

    bool IsHlsSupported(StreamId stream_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr && source_state::IsHlsStreamReady(*stream);
    }

    bool IsFlvSupported(StreamId stream_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr && source_state::IsFlvStreamReady(*stream);
    }

    bool IsMjpegSupported(StreamId stream_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr && source_state::IsMjpegStreamReady(*stream);
    }

    bool IsStreamAvailable(StreamId stream_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr && stream->state == MediaStreamState::kRunning;
    }

    Codec GetStreamCodec(StreamId stream_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr ? stream->codec : Codec::kH264;
    }

    MediaHlsPlaylist GetHlsPlaylist(StreamId stream_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return MediaHlsPlaylist{};
        }
        stream->hls_maker.MarkRequested();
        return source_state::BuildHlsPlaylist(*stream,
                                              options_.hls_segment_duration_ms,
                                              options_.hls_playlist_depth);
    }

    MediaSegmentRef GetHlsSegmentRef(StreamId stream_id,
                                     uint64_t sequence) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return MediaSegmentRef{};
        }
        stream->hls_maker.MarkRequested();
        return source_state::FindHlsSegmentRef(*stream, sequence);
    }

    MediaFlvStartData GetFlvStartData(StreamId stream_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return MediaFlvStartData{};
        }
        return source_state::BuildFlvStartData(*stream);
    }

    MediaStreamInfo GetStreamInfo(StreamId stream_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        MediaStreamInfo info;
        const source_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return info;
        }
        info.running = stream->state == MediaStreamState::kRunning;
        info.hls_supported = source_state::IsHlsCodecSupported(stream->codec);
        info.flv_supported = source_state::IsFlvCodecSupported(stream->codec);
        info.mjpeg_supported =
            source_state::IsMjpegCodecSupported(stream->codec);
        info.browser_codec =
            info.hls_supported || info.flv_supported || info.mjpeg_supported;
        info.track_ready =
            source_state::BuildMediaTrack(stream_id, *stream).ready;
        info.hls_ready = source_state::IsHlsStreamReady(*stream);
        info.flv_ready = source_state::IsFlvStreamReady(*stream);
        info.mjpeg_ready = source_state::IsMjpegStreamReady(*stream);
        info.codec = stream->codec;
        info.codec_generation = stream->codec_generation;
        info.hls_segment_count =
            static_cast<uint32_t>(stream->hls_maker.SegmentCount());
        info.hls_first_segment_sequence =
            stream->hls_maker.FirstSegmentSequence();
        info.hls_last_segment_sequence =
            stream->hls_maker.LastSegmentSequence();
        info.hls_missing_segment_count =
            stream->hls_maker.MissingSegmentCount();
        info.hls_evicted_segment_count =
            stream->hls_maker.EvictedSegmentCount();
        info.flv_sequence_header_size =
            static_cast<uint32_t>(stream->sequence_header_tag.size());
        info.flv_last_keyframe_size =
            stream->flv_gop_cache.FirstFlvTagSize();
        info.hls_current_segment_size =
            stream->hls_maker.CurrentSegmentSize();
        info.last_dts_us = stream->timestamp_corrector.last_dts_us();
        info.last_reset_reason =
            MediaStreamResetReasonName(stream->last_reset_reason);
        return info;
    }

    MediaStreamCounters GetStreamCounters() const {
        std::lock_guard<std::mutex> guard(mutex_);
        MediaStreamCounters counters = counters_;
        counters.enabled = started_;
        counters.active_flv_clients =
            static_cast<uint32_t>(flv_live_ring_.ReaderCount());
        counters.active_mjpeg_clients =
            static_cast<uint32_t>(mjpeg_clients_.Size());
        counters.active_frame_subscriptions =
            static_cast<uint32_t>(frame_ring_.ReaderCount());
        counters.cached_frames = frame_ring_.CachedFrameCount();
        counters.cached_bytes = frame_ring_.CachedBytes();
        counters.slow_subscriber_count = frame_ring_.SlowReaderCount();
        counters.main_slow_subscriber_count =
            frame_ring_.SlowReaderCount(StreamId::kMain);
        counters.sub_slow_subscriber_count =
            frame_ring_.SlowReaderCount(StreamId::kSub);
        counters.main_last_frame_timestamp_us =
            frame_ring_.LastFrameTimestamp(StreamId::kMain);
        counters.sub_last_frame_timestamp_us =
            frame_ring_.LastFrameTimestamp(StreamId::kSub);
        counters.main_codec_generation = main_stream_.codec_generation;
        counters.sub_codec_generation = sub_stream_.codec_generation;
        counters.main_last_reset_reason =
            MediaStreamResetReasonName(main_stream_.last_reset_reason);
        counters.sub_last_reset_reason =
            MediaStreamResetReasonName(sub_stream_.last_reset_reason);
        return counters;
    }

    MediaFlvClientId AttachFlvClient(StreamId stream_id,
                                     uint64_t config_generation,
                                     bool wait_for_keyframe,
                                     IMediaFlvSink *sink) {
        if (sink == nullptr) {
            return 0;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        source_state::StreamContext *stream = FindMutableStream(stream_id);
        if (!started_ || stream == nullptr ||
            !source_state::IsFlvStreamReady(*stream) ||
            flv_live_ring_.ReaderCount() >= options_.max_flv_clients) {
            return 0;
        }
        return flv_live_ring_.AttachReader(
            stream_id, config_generation, wait_for_keyframe, sink,
            options_.max_flv_clients);
    }

    bool DetachFlvClient(MediaFlvClientId client_id) {
        std::lock_guard<std::mutex> guard(mutex_);
        return flv_live_ring_.DetachReader(client_id);
    }

    MediaMjpegClientId AttachMjpegClient(StreamId stream_id,
                                         IMediaMjpegSink *sink) {
        if (sink == nullptr) {
            return 0;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        source_state::StreamContext *stream = FindMutableStream(stream_id);
        if (!started_ || stream == nullptr ||
            !source_state::IsMjpegStreamReady(*stream) ||
            mjpeg_clients_.Size() >= options_.max_mjpeg_clients) {
            return 0;
        }
        return mjpeg_clients_.Attach(stream_id, sink,
                                     options_.max_mjpeg_clients);
    }

    bool DetachMjpegClient(MediaMjpegClientId client_id) {
        std::lock_guard<std::mutex> guard(mutex_);
        return mjpeg_clients_.Detach(client_id);
    }

    FrameSubscriptionId SubscribeFrames(
        const FrameSubscriptionOptions &options) {
        if (!IsStreamSupported(options.stream_id)) {
            return 0;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        if (!started_ || FindStream(options.stream_id) == nullptr ||
            frame_ring_.ReaderCount() >= options_.max_frame_subscriptions) {
            return 0;
        }
        return frame_ring_.AttachReader(options,
                                        options_.max_frame_subscriptions);
    }

    bool UnsubscribeFrames(FrameSubscriptionId subscription_id,
                           FrameSubscriptionCloseReason reason) {
        std::lock_guard<std::mutex> guard(mutex_);
        return frame_ring_.DetachReader(subscription_id, reason);
    }

    FrameSubscriptionInfo GetFrameSubscriptionInfo(
        FrameSubscriptionId subscription_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        return frame_ring_.GetReaderStatus(subscription_id);
    }

    FrameSubscriptionStartData GetFrameSubscriptionStartData(
        FrameSubscriptionId subscription_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const FrameSubscriptionInfo subscription_info =
            frame_ring_.GetReaderStatus(subscription_id);
        if (!subscription_info.attached) {
            return FrameSubscriptionStartData{};
        }
        const source_state::StreamContext *stream =
            FindStream(subscription_info.stream_id);
        if (stream == nullptr) {
            return FrameSubscriptionStartData{};
        }
        const MediaTrack track =
            source_state::BuildMediaTrack(subscription_info.stream_id,
                                          *stream);
        return frame_ring_.GetStartData(subscription_id, track);
    }

    bool PopSubscribedFrame(FrameSubscriptionId subscription_id,
                            SubscribedFrame *frame) {
        std::lock_guard<std::mutex> guard(mutex_);
        return frame_ring_.PopFrame(subscription_id, frame);
    }

private:
    bool ValidateOptions() const {
        return options_.hls_segment_duration_ms != 0 &&
               options_.hls_playlist_depth != 0 &&
               HlsSegmentCacheDepth(options_) >= options_.hls_playlist_depth &&
               options_.max_flv_clients != 0 &&
               options_.max_mjpeg_clients != 0 &&
               options_.max_frame_subscriptions != 0;
    }

    void ResetRuntimeStateLocked() {
        frame_ring_.Clear();
        flv_live_ring_.Clear();
        mjpeg_clients_.Clear();
        source_state::ClearStreamContext(&main_stream_);
        source_state::ClearStreamContext(&sub_stream_);
        counters_ = MediaStreamCounters{};
    }

    void EnsureRunningStreamLocked(source_state::StreamContext *stream,
                                   StreamId stream_id,
                                   Codec codec) {
        if (stream == nullptr) {
            return;
        }
        if (stream->state != MediaStreamState::kRunning) {
            ResetStreamForReasonLocked(stream_id, codec,
                                       MediaStreamResetReason::kStreamStarted);
            stream->codec = codec;
            stream->state = MediaStreamState::kRunning;
            return;
        }
        if (stream->codec != codec) {
            ResetStreamForReasonLocked(stream_id, codec,
                                       MediaStreamResetReason::kCodecChanged);
            stream->codec = codec;
            stream->state = MediaStreamState::kRunning;
        }
    }

    void QueueFrameForSubscribers(
        const source_state::ParsedFramePayload &payload) {
        std::lock_guard<std::mutex> guard(mutex_);
        source_state::StreamContext *stream =
            FindMutableStream(payload.encoded_frame.stream_id);
        if (stream == nullptr ||
            stream->state != MediaStreamState::kRunning) {
            return;
        }
        frame_ring_.Write(payload);
    }

    void PackageBrowserFrame(const source_state::ParsedFramePayload &payload,
                             bool has_payload) {
        if (!has_payload) {
            PackageMjpegFrame(payload);
            return;
        }

        const EncodedFrame &frame = payload.encoded_frame;
        std::vector<source_state::PendingFlvClientWrite> clients;
        std::string sequence_header_tag;
        source_state::FlvVideoTagView flv_tag_view;
        bool has_flv_tag_view = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            source_state::StreamContext *stream =
                FindMutableStream(frame.stream_id);
            if (stream == nullptr) {
                return;
            }
            EnsureRunningStreamLocked(stream, frame.stream_id, frame.codec);
            if (!source_state::IsBrowserStreamReady(stream->state,
                                                    stream->codec)) {
                return;
            }
            const bool package_hls =
                source_state::IsHlsCodecSupported(stream->codec);
            const bool package_flv = flv_live_ring_.HasReader(frame.stream_id);
            const bool update_flv_cache =
                source_state::IsFlvCodecSupported(stream->codec);
            if (!package_hls && !package_flv && !update_flv_cache) {
                return;
            }

            source_state::PackagedFrameResult packaged_frame =
                source_state::AppendFrameToStream(
                    stream, frame, payload, package_hls,
                    package_flv || update_flv_cache,
                    options_.hls_segment_duration_ms,
                    HlsSegmentCacheDepth(options_));
            if (!packaged_frame.accepted) {
                return;
            }
            if (packaged_frame.hls_segment_created) {
                ++counters_.hls_segments_created;
            }
            flv_tag_view = packaged_frame.flv_tag_view;
            has_flv_tag_view = packaged_frame.has_flv_tag_view;
            const bool has_sequence_header =
                source_state::HasFlvSequenceHeader(*stream);
            clients = flv_live_ring_.CollectWrites(
                frame.stream_id, stream->config_generation,
                has_flv_tag_view, has_sequence_header,
                packaged_frame.keyframe);
            for (const source_state::PendingFlvClientWrite &client : clients) {
                if (client.send_sequence_header) {
                    sequence_header_tag = stream->sequence_header_tag;
                    break;
                }
            }
        }
        WriteFlvClients(clients, sequence_header_tag, flv_tag_view,
                        has_flv_tag_view, frame);
    }

    void PackageMjpegFrame(const source_state::ParsedFramePayload &payload) {
        const EncodedFrame &frame = payload.encoded_frame;
        if (frame.codec != Codec::kMjpeg || !EncodedFrameHasPayload(&frame)) {
            return;
        }

        std::vector<source_clients::PendingMjpegClientWrite> clients;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            source_state::StreamContext *stream =
                FindMutableStream(frame.stream_id);
            if (stream == nullptr) {
                return;
            }
            EnsureRunningStreamLocked(stream, frame.stream_id, frame.codec);
            if (!source_state::StoreMjpegFrame(stream, frame)) {
                return;
            }
            if (mjpeg_clients_.HasClient(frame.stream_id)) {
                clients = mjpeg_clients_.CollectWrites(frame.stream_id);
            }
        }
        WriteMjpegClients(clients, frame);
    }

    void WriteFlvClients(
        const std::vector<source_state::PendingFlvClientWrite> &clients,
        const std::string &sequence_header_tag,
        const source_state::FlvVideoTagView &flv_tag_view,
        bool has_flv_tag_view,
        const EncodedFrame &frame) {
        std::vector<MediaFlvClientId> detach_ids;
        for (const source_state::PendingFlvClientWrite &client : clients) {
            if (client.send_sequence_header &&
                !client.sink->OnFlvChunk(
                    reinterpret_cast<const uint8_t *>(
                        sequence_header_tag.data()),
                    sequence_header_tag.size())) {
                detach_ids.push_back(client.client_id);
                ReleaseFlvClientWrite(client.client_id);
                continue;
            }
            if (!has_flv_tag_view) {
                detach_ids.push_back(client.client_id);
                ReleaseFlvClientWrite(client.client_id);
                continue;
            }
            const MediaFlvVideoTagView flv_video_tag =
                ToMediaFlvVideoTagView(flv_tag_view);
            if (!client.sink->OnFlvVideoTag(flv_video_tag, frame)) {
                detach_ids.push_back(client.client_id);
            }
            ReleaseFlvClientWrite(client.client_id);
        }
        for (MediaFlvClientId client_id : detach_ids) {
            if (client_id != 0) {
                (void)DetachFlvClient(client_id);
            }
        }
    }

    void ReleaseFlvClientWrite(MediaFlvClientId client_id) {
        std::lock_guard<std::mutex> guard(mutex_);
        flv_live_ring_.ReleaseWrite(client_id);
    }

    void WriteMjpegClients(
        const std::vector<source_clients::PendingMjpegClientWrite> &clients,
        const EncodedFrame &frame) {
        std::vector<MediaMjpegClientId> detach_ids;
        for (const source_clients::PendingMjpegClientWrite &client : clients) {
            if (client.sink == nullptr || !client.sink->OnMjpegFrame(frame)) {
                detach_ids.push_back(client.client_id);
            }
            ReleaseMjpegClientWrite(client.client_id);
        }
        for (MediaMjpegClientId client_id : detach_ids) {
            if (client_id != 0) {
                (void)DetachMjpegClient(client_id);
            }
        }
    }

    void ReleaseMjpegClientWrite(MediaMjpegClientId client_id) {
        std::lock_guard<std::mutex> guard(mutex_);
        mjpeg_clients_.ReleaseWrite(client_id);
    }

    const source_state::StreamContext *FindStream(StreamId stream_id) const {
        if (stream_id == StreamId::kMain) {
            return &main_stream_;
        }
        if (stream_id == StreamId::kSub) {
            return &sub_stream_;
        }
        return nullptr;
    }

    source_state::StreamContext *FindMutableStream(StreamId stream_id) {
        if (stream_id == StreamId::kMain) {
            return &main_stream_;
        }
        if (stream_id == StreamId::kSub) {
            return &sub_stream_;
        }
        return nullptr;
    }

    void ResetStreamForReasonLocked(StreamId stream_id, Codec codec,
                                    MediaStreamResetReason reason) {
        source_state::StreamContext *stream = FindMutableStream(stream_id);
        if (stream == nullptr) {
            return;
        }
        source_state::ResetStream(stream, codec, reason);
        frame_ring_.ClearStream(stream_id, CloseReasonForReset(reason));
    }

    void SetStreamStateLocked(StreamId stream_id, MediaStreamState state,
                              Codec codec) {
        source_state::StreamContext *stream = FindMutableStream(stream_id);
        if (stream == nullptr) {
            return;
        }
        if (state == MediaStreamState::kRunning) {
            EnsureRunningStreamLocked(stream, stream_id, codec);
            return;
        }
        ResetStreamForReasonLocked(stream_id, stream->codec,
                                   MediaStreamResetReason::kStreamStopped);
        stream->state = state;
    }

    MediaStreamsOptions options_;
    mutable std::mutex mutex_;
    bool started_ = false;
    source_state::StreamContext main_stream_;
    source_state::StreamContext sub_stream_;
    source_state::FrameRing frame_ring_;
    source_state::FlvLiveRing flv_live_ring_;
    source_clients::MjpegClientRegistry mjpeg_clients_;
    MediaStreamCounters counters_;
};

MediaStreams::MediaStreams(MediaStreamsOptions options)
    : impl_(new Impl(std::move(options))) {}

MediaStreams::~MediaStreams() = default;

bool MediaStreams::Start() { return impl_->Start(); }

void MediaStreams::Stop() { impl_->Stop(); }

bool MediaStreams::PushFrame(const EncodedFrame &frame) {
    return impl_->PushFrame(frame);
}

void MediaStreams::SetStreamState(StreamId stream_id, MediaStreamState state,
                                  Codec codec) {
    impl_->SetStreamState(stream_id, state, codec);
}

bool MediaStreams::IsHlsSupported(StreamId stream_id) const {
    return impl_->IsHlsSupported(stream_id);
}

bool MediaStreams::IsFlvSupported(StreamId stream_id) const {
    return impl_->IsFlvSupported(stream_id);
}

bool MediaStreams::IsMjpegSupported(StreamId stream_id) const {
    return impl_->IsMjpegSupported(stream_id);
}

bool MediaStreams::IsStreamAvailable(StreamId stream_id) const {
    return impl_->IsStreamAvailable(stream_id);
}

Codec MediaStreams::GetStreamCodec(StreamId stream_id) const {
    return impl_->GetStreamCodec(stream_id);
}

MediaHlsPlaylist MediaStreams::GetHlsPlaylist(StreamId stream_id) const {
    return impl_->GetHlsPlaylist(stream_id);
}

MediaSegmentRef MediaStreams::GetHlsSegmentRef(
    StreamId stream_id, uint64_t sequence) const {
    return impl_->GetHlsSegmentRef(stream_id, sequence);
}

MediaFlvStartData MediaStreams::GetFlvStartData(StreamId stream_id) const {
    return impl_->GetFlvStartData(stream_id);
}

MediaStreamInfo MediaStreams::GetStreamInfo(StreamId stream_id) const {
    return impl_->GetStreamInfo(stream_id);
}

MediaStreamCounters MediaStreams::GetStreamCounters() const {
    return impl_->GetStreamCounters();
}

MediaFlvClientId MediaStreams::AttachFlvClient(
    StreamId stream_id, uint64_t config_generation, bool wait_for_keyframe,
    IMediaFlvSink *sink) {
    return impl_->AttachFlvClient(stream_id, config_generation,
                                  wait_for_keyframe, sink);
}

bool MediaStreams::DetachFlvClient(MediaFlvClientId client_id) {
    return impl_->DetachFlvClient(client_id);
}

MediaMjpegClientId MediaStreams::AttachMjpegClient(StreamId stream_id,
                                                   IMediaMjpegSink *sink) {
    return impl_->AttachMjpegClient(stream_id, sink);
}

bool MediaStreams::DetachMjpegClient(MediaMjpegClientId client_id) {
    return impl_->DetachMjpegClient(client_id);
}

FrameSubscriptionId MediaStreams::SubscribeFrames(
    const FrameSubscriptionOptions &options) {
    return impl_->SubscribeFrames(options);
}

bool MediaStreams::UnsubscribeFrames(FrameSubscriptionId subscription_id,
                                     FrameSubscriptionCloseReason reason) {
    return impl_->UnsubscribeFrames(subscription_id, reason);
}

FrameSubscriptionInfo MediaStreams::GetFrameSubscriptionInfo(
    FrameSubscriptionId subscription_id) const {
    return impl_->GetFrameSubscriptionInfo(subscription_id);
}

FrameSubscriptionStartData MediaStreams::GetFrameSubscriptionStartData(
    FrameSubscriptionId subscription_id) const {
    return impl_->GetFrameSubscriptionStartData(subscription_id);
}

bool MediaStreams::PopSubscribedFrame(FrameSubscriptionId subscription_id,
                                      SubscribedFrame *frame) {
    return impl_->PopSubscribedFrame(subscription_id, frame);
}

}  // namespace live_stream
