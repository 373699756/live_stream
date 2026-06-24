#include "media/media_streams.h"

#include "media_streams_impl.h"

#include <utility>

namespace live_stream {

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
