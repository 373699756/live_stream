#include "media/media_streams.h"

#include "flv_live_ring.h"
#include "frame_ring.h"
#include "media_stream_state.h"
#include "media_codec.h"
#include "mjpeg_clients.h"

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

SubscriptionClose CloseReasonForReset(
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

const char *SubscriptionCloseName(
    SubscriptionClose reason) {
    switch (reason) {
        case SubscriptionClose::kNone:
            return "none";
        case SubscriptionClose::kUnsubscribed:
            return "unsubscribed";
        case SubscriptionClose::kStreamStopped:
            return "stream_stopped";
        case SubscriptionClose::kCodecChanged:
            return "codec_changed";
        case SubscriptionClose::kTimestampReset:
            return "timestamp_reset";
        case SubscriptionClose::kCacheOverflow:
            return "cache_overflow";
    }
    return "unknown";
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
        ResetMediaStateLocked();
        started_ = true;
        return true;
    }

    void Stop() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!started_) {
            return;
        }
        ResetMediaStateLocked();
        started_ = false;
    }

    bool PushFrame(const MediaFrame &input_frame) {
        if (!IsMediaFramePayloadValid(input_frame) ||
            !IsStreamSupported(input_frame.stream_id)) {
            return false;
        }

        MediaFrame frame = input_frame;

        bool normalized = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!started_) {
                return false;
            }
            source_state::StreamContext *stream =
                FindMutableStream(frame.stream_id);
            if (stream == nullptr) {
                return false;
            }
            EnsureRunningStreamLocked(stream, frame.stream_id, frame.codec);
            const source_state::NormalizedFrameResult result =
                source_state::NormalizeFrameTimestamps(stream, &frame);
            if (result.timestamp_reset) {
                frame_ring_.ClearStream(
                    frame.stream_id,
                    SubscriptionClose::kTimestampReset);
            }
            normalized = result.accepted;
        }

        if (!normalized) {
            return false;
        }

        source_state::ParsedFramePayload payload;
        source_state::ParseFramePayload(frame, &payload);
        QueueFrameForSubscriptions(payload);
        PackageBrowserFrame(payload,
                            source_state::IsFramePayloadParsed(payload));
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

    MediaFlvStart GetFlvStart(StreamId stream_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return MediaFlvStart{};
        }
        return source_state::BuildFlvStart(*stream);
    }

    MediaStreamInfo GetStreamInfo(StreamId stream_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return MediaStreamInfo{};
        }
        return source_state::BuildMediaStreamInfo(*stream);
    }

    MediaStreamStats GetStreamStats() const {
        std::lock_guard<std::mutex> guard(mutex_);
        MediaStreamStats stats = stats_;
        stats.enabled = started_;
        stats.active_flv_clients =
            static_cast<uint32_t>(flv_live_ring_.ClientCount());
        stats.active_mjpeg_clients =
            static_cast<uint32_t>(mjpeg_clients_.Size());
        stats.active_subscriptions =
            static_cast<uint32_t>(frame_ring_.SubscriptionCount());
        stats.cached_frames = frame_ring_.CachedFrameCount();
        stats.cached_bytes = frame_ring_.CachedBytes();
        stats.slow_subscriptions = frame_ring_.SlowSubscriptionCount();
        stats.main_slow_subscriptions =
            frame_ring_.SlowSubscriptionCount(StreamId::kMain);
        stats.sub_slow_subscriptions =
            frame_ring_.SlowSubscriptionCount(StreamId::kSub);
        stats.main_last_frame_timestamp_us =
            frame_ring_.LastFrameTimestamp(StreamId::kMain);
        stats.sub_last_frame_timestamp_us =
            frame_ring_.LastFrameTimestamp(StreamId::kSub);
        stats.main_codec_generation = main_stream_.codec_generation;
        stats.sub_codec_generation = sub_stream_.codec_generation;
        stats.main_last_reset_reason =
            MediaStreamResetReasonName(main_stream_.last_reset_reason);
        stats.sub_last_reset_reason =
            MediaStreamResetReasonName(sub_stream_.last_reset_reason);
        return stats;
    }

    bool RequestKeyframe(StreamId stream_id,
                         KeyframeRequestSource source) {
        if (!IsStreamSupported(stream_id) ||
            options_.request_keyframe == nullptr) {
            return false;
        }
        return options_.request_keyframe(stream_id, source,
                                         options_.request_keyframe_user);
    }

    MediaFlvClientId AttachFlvClient(StreamId stream_id,
                                     uint64_t config_generation,
                                     bool wait_for_keyframe,
                                     IMediaFlvSink *sink) {
        if (sink == nullptr) {
            return 0;
        }
        MediaFlvClientId client_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            source_state::StreamContext *stream = FindMutableStream(stream_id);
            if (!started_ || stream == nullptr ||
                !source_state::IsFlvStreamReady(*stream) ||
                flv_live_ring_.ClientCount() >= options_.max_flv_clients) {
                return 0;
            }
            client_id = flv_live_ring_.AttachClient(
                stream_id, config_generation, wait_for_keyframe, sink,
                options_.max_flv_clients);
        }
        if (client_id != 0) {
            (void)RequestKeyframe(stream_id, KeyframeRequestSource::kNewClient);
        }
        return client_id;
    }

    bool DetachFlvClient(MediaFlvClientId client_id) {
        std::lock_guard<std::mutex> guard(mutex_);
        return flv_live_ring_.DetachClient(client_id);
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
        const SubscriptionOptions &options) {
        if (!IsStreamSupported(options.stream_id)) {
            return 0;
        }
        FrameSubscriptionId subscription_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!started_ || FindStream(options.stream_id) == nullptr ||
                frame_ring_.SubscriptionCount() >=
                    options_.max_frame_subscriptions) {
                return 0;
            }
            subscription_id = frame_ring_.AttachSubscription(
                options, options_.max_frame_subscriptions);
        }
        if (subscription_id != 0 && options.keyframe_first) {
            (void)RequestKeyframe(options.stream_id,
                                  KeyframeRequestSource::kNewClient);
        }
        return subscription_id;
    }

    bool UnsubscribeFrames(FrameSubscriptionId subscription_id,
                           SubscriptionClose reason) {
        std::lock_guard<std::mutex> guard(mutex_);
        return frame_ring_.DetachSubscription(subscription_id, reason);
    }

    SubscriptionInfo GetSubscriptionInfo(
        FrameSubscriptionId subscription_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        return frame_ring_.GetSubscriptionInfo(subscription_id);
    }

    SubscriptionStart GetSubscriptionStart(
        FrameSubscriptionId subscription_id) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const SubscriptionInfo subscription_info =
            frame_ring_.GetSubscriptionInfo(subscription_id);
        if (!subscription_info.open) {
            return SubscriptionStart{};
        }
        const source_state::StreamContext *stream =
            FindStream(subscription_info.stream_id);
        if (stream == nullptr) {
            return SubscriptionStart{};
        }
        const MediaStreamInfo stream_info =
            source_state::BuildMediaStreamInfo(*stream);
        return frame_ring_.GetSubscriptionStart(subscription_id, stream_info);
    }

    bool PopSubscriptionFrame(FrameSubscriptionId subscription_id,
                              SubscriptionFrame *frame) {
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

    void ResetMediaStateLocked() {
        frame_ring_.Clear();
        flv_live_ring_.Clear();
        mjpeg_clients_.Clear();
        source_state::ClearStreamContext(&main_stream_);
        source_state::ClearStreamContext(&sub_stream_);
        stats_ = MediaStreamStats{};
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

    void QueueFrameForSubscriptions(
        const source_state::ParsedFramePayload &payload) {
        std::lock_guard<std::mutex> guard(mutex_);
        source_state::StreamContext *stream =
            FindMutableStream(payload.frame.stream_id);
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

        const MediaFrame &frame = payload.frame;
        std::vector<source_state::PendingFlvClientWrite> clients;
        std::string sequence_header_tag;
        source_state::FlvVideoTagBuild flv_tag_view;
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
            const bool package_flv =
                flv_live_ring_.IsStreamClientAttached(frame.stream_id);
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
                ++stats_.hls_segments_created;
            }
            flv_tag_view = packaged_frame.flv_tag_view;
            has_flv_tag_view = packaged_frame.has_flv_tag_view;
            const bool has_sequence_header =
                source_state::IsFlvSequenceHeaderReady(*stream);
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
        const MediaFrame &frame = payload.frame;
        if (frame.codec != Codec::kMjpeg ||
            !IsMediaFramePayloadValid(frame)) {
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
            if (!source_state::CacheMjpegFrame(stream, frame)) {
                return;
            }
            if (mjpeg_clients_.IsStreamClientAttached(frame.stream_id)) {
                clients = mjpeg_clients_.CollectWrites(frame.stream_id);
            }
        }
        WriteMjpegClients(clients, frame);
    }

    void WriteFlvClients(
        const std::vector<source_state::PendingFlvClientWrite> &clients,
        const std::string &sequence_header_tag,
        const source_state::FlvVideoTagBuild &flv_tag_view,
        bool has_flv_tag_view,
        const MediaFrame &frame) {
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
            if (!client.sink->OnFlvVideoTag(flv_tag_view.view, frame)) {
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
        const MediaFrame &frame) {
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
    source_clients::MjpegClients mjpeg_clients_;
    MediaStreamStats stats_;
};

MediaStreams::MediaStreams(MediaStreamsOptions options)
    : impl_(new Impl(std::move(options))) {}

MediaStreams::~MediaStreams() = default;

bool MediaStreams::Start() { return impl_->Start(); }

void MediaStreams::Stop() { impl_->Stop(); }

bool MediaStreams::PushFrame(const MediaFrame &frame) {
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

MediaFlvStart MediaStreams::GetFlvStart(StreamId stream_id) const {
    return impl_->GetFlvStart(stream_id);
}

MediaStreamInfo MediaStreams::GetStreamInfo(StreamId stream_id) const {
    return impl_->GetStreamInfo(stream_id);
}

MediaStreamStats MediaStreams::GetStreamStats() const {
    return impl_->GetStreamStats();
}

bool MediaStreams::RequestKeyframe(StreamId stream_id,
                                   KeyframeRequestSource source) {
    return impl_->RequestKeyframe(stream_id, source);
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
    const SubscriptionOptions &options) {
    return impl_->SubscribeFrames(options);
}

bool MediaStreams::UnsubscribeFrames(FrameSubscriptionId subscription_id,
                                     SubscriptionClose reason) {
    return impl_->UnsubscribeFrames(subscription_id, reason);
}

SubscriptionInfo MediaStreams::GetSubscriptionInfo(
    FrameSubscriptionId subscription_id) const {
    return impl_->GetSubscriptionInfo(subscription_id);
}

SubscriptionStart MediaStreams::GetSubscriptionStart(
    FrameSubscriptionId subscription_id) const {
    return impl_->GetSubscriptionStart(subscription_id);
}

bool MediaStreams::PopSubscriptionFrame(FrameSubscriptionId subscription_id,
                                        SubscriptionFrame *frame) {
    return impl_->PopSubscriptionFrame(subscription_id, frame);
}

}  // namespace live_stream
