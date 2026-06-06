#include "media_source_service.h"

#include "flv_client_registry.h"
#include "infra/executor.h"
#include "infra/log.h"
#include "media/encoded_frame.h"
#include "mjpeg_client_registry.h"
#include "media_source_stream_state.h"
#include "media_frame_dispatcher.h"
#include "stream_codec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace source_state = media_source_internal;
namespace source_clients = media_source_service_internal;
namespace {

constexpr const char *kServiceName = "media_source_service";
constexpr uint32_t kWorkerQueueCapacity = 4;
constexpr uint32_t kWorkerThreadCount = 1;
constexpr size_t kMaxPendingFramesPerStream = 4;

uint32_t HlsSegmentCacheDepth(const MediaSourceServiceOptions &options) {
    return options.hls_playlist_depth + options.hls_segment_retain_count;
}

class PendingFrameQueue {
public:
    void Clear() {
        for (EncodedFrame &frame : frames_) {
            EncodedFrameUnref(&frame);
        }
        head_ = 0;
        size_ = 0;
    }

    bool Empty() const { return size_ == 0; }

    bool Full() const { return size_ >= frames_.size(); }

    bool PushBack(const EncodedFrame &frame) {
        if (Full()) {
            return false;
        }
        if (!EncodedFrameRefCopy(&frames_[(head_ + size_) % frames_.size()],
                                    &frame)) {
            return false;
        }
        ++size_;
        return true;
    }

    bool PopFront() {
        if (Empty()) {
            return false;
        }
        EncodedFrameUnref(&frames_[head_]);
        head_ = (head_ + 1) % frames_.size();
        --size_;
        return true;
    }

    bool DropOldestNonKeyFrame() {
        for (size_t i = 0; i < size_; ++i) {
            const size_t index = (head_ + i) % frames_.size();
            if (!stream_codec::IsKeyFrame(frames_[index].frame_type)) {
                RemoveAt(i);
                return true;
            }
        }
        return false;
    }

    bool TakeFront(EncodedFrame *frame) {
        if (frame == nullptr || Empty()) {
            return false;
        }
        (void)EncodedFrameMove(frame, &frames_[head_]);
        head_ = (head_ + 1) % frames_.size();
        --size_;
        return true;
    }

private:
    void RemoveAt(size_t position) {
        if (position >= size_) {
            return;
        }
        for (size_t i = position; i + 1 < size_; ++i) {
            const size_t target = (head_ + i) % frames_.size();
            const size_t source = (head_ + i + 1) % frames_.size();
            (void)EncodedFrameMove(&frames_[target], &frames_[source]);
        }
        const size_t tail = (head_ + size_ - 1) % frames_.size();
        EncodedFrameUnref(&frames_[tail]);
        --size_;
    }

    std::array<EncodedFrame, kMaxPendingFramesPerStream> frames_{};
    size_t head_ = 0;
    size_t size_ = 0;
};

MediaFlvVideoTagView ToMediaFlvVideoTagView(
    const stream_mux::FlvVideoTagView &tag) {
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

bool IsStreamSupported(StreamId stream_id) {
    return stream_id == StreamId::kMain || stream_id == StreamId::kSub;
}

const char *StreamName(StreamId stream_id) {
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

const char *CodecName(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::kH264:
            return "h264";
        case VideoCodec::kH265:
            return "h265";
        case VideoCodec::kMjpeg:
            return "mjpeg";
        case VideoCodec::kJpeg:
            return "jpeg";
    }
    return "unknown";
}

class MediaSourceServiceImpl : public IMediaSourceService, public IFrameSink {
public:
    MediaSourceServiceImpl(MediaSourceServiceOptions options,
                           MediaSourceServiceDependencies dependencies)
        : options_(std::move(options)), dependencies_(dependencies) {}

    ~MediaSourceServiceImpl() override { StopInternal(); }

    bool Start() override {
        IMediaService *media_service = nullptr;
        infra::Executor *worker_executor = nullptr;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (run_state_ == MediaSourceRunState::kStarted) {
                return true;
            }
            if (run_state_ == MediaSourceRunState::kStarting) {
                return false;
            }
            media_service = dependencies_.media_service;
            if (!worker_executor_) {
                worker_executor_.reset(new infra::Executor());
            }
            worker_executor = worker_executor_.get();
            run_state_ = MediaSourceRunState::kStarting;
        }
        if (media_service == nullptr || worker_executor == nullptr) {
            std::lock_guard<std::mutex> guard(mutex_);
            ResetRuntimeStateLocked();
            run_state_ = MediaSourceRunState::kStopped;
            return false;
        }
        if (options_.hls_segment_duration_ms == 0 ||
            options_.hls_playlist_depth == 0 ||
            HlsSegmentCacheDepth(options_) < options_.hls_playlist_depth ||
            options_.max_flv_clients == 0 ||
            options_.max_mjpeg_clients == 0 || options_.max_frame_sinks == 0) {
            std::lock_guard<std::mutex> guard(mutex_);
            ResetRuntimeStateLocked();
            run_state_ = MediaSourceRunState::kStopped;
            return false;
        }
        infra::ExecutorOptions executor_options;
        executor_options.worker_count = kWorkerThreadCount;
        executor_options.queue_capacity = kWorkerQueueCapacity;
        if (!worker_executor->Start(executor_options)) {
            std::lock_guard<std::mutex> guard(mutex_);
            ResetRuntimeStateLocked();
            run_state_ = MediaSourceRunState::kStopped;
            return false;
        }
        const VideoCodec main_codec =
            media_service->GetStreamCodec(StreamId::kMain);
        const VideoCodec sub_codec =
            media_service->GetStreamCodec(StreamId::kSub);
        {
            std::lock_guard<std::mutex> guard(mutex_);
            ResetRuntimeStateLocked();
            main_stream_.codec = main_codec;
            main_stream_.state = StreamState::kClosed;
            sub_stream_.codec = sub_codec;
            sub_stream_.state = StreamState::kClosed;
        }

        FrameAttachOptions main_options;
        main_options.stream_id = StreamId::kMain;
        main_options.require_key_frame_first = true;
        main_options.sink_name = kServiceName;
        const FrameAttachId main_attach_id =
            media_service->AttachFrameSink(main_options, this);

        FrameAttachOptions sub_options;
        sub_options.stream_id = StreamId::kSub;
        sub_options.require_key_frame_first = true;
        sub_options.sink_name = kServiceName;
        const FrameAttachId sub_attach_id =
            media_service->AttachFrameSink(sub_options, this);
        const bool has_media_attachment =
            main_attach_id != 0 || sub_attach_id != 0;
        if (!has_media_attachment) {
            {
                std::lock_guard<std::mutex> guard(mutex_);
                ResetRuntimeStateLocked();
                run_state_ = MediaSourceRunState::kStopped;
            }
            worker_executor->Stop(infra::StopMode::kDiscard);
            return false;
        }
        bool need_main_key_frame = false;
        bool need_sub_key_frame = false;
        bool start_was_cancelled = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (run_state_ != MediaSourceRunState::kStarting) {
                start_was_cancelled = true;
            } else {
                main_attach_id_ = main_attach_id;
                sub_attach_id_ = sub_attach_id;
                run_state_ = MediaSourceRunState::kStarted;
                need_main_key_frame = main_attach_id_ != 0;
                need_sub_key_frame = sub_attach_id_ != 0;
            }
        }
        if (start_was_cancelled) {
            {
                std::lock_guard<std::mutex> guard(mutex_);
                ResetRuntimeStateLocked();
                run_state_ = MediaSourceRunState::kStopped;
            }
            if (main_attach_id != 0) {
                (void)media_service->DetachFrameSink(main_attach_id);
            }
            if (sub_attach_id != 0) {
                (void)media_service->DetachFrameSink(sub_attach_id);
            }
            worker_executor->Stop(infra::StopMode::kDiscard);
            return false;
        }
        INFRA_LOG_INFO(kServiceName,
                       "media source attached main_attach_id=%llu sub_attach_id=%llu "
                       "main_codec=%s sub_codec=%s need_main_key_frame=%d "
                       "need_sub_key_frame=%d",
                       static_cast<unsigned long long>(main_attach_id),
                       static_cast<unsigned long long>(sub_attach_id),
                       CodecName(main_codec), CodecName(sub_codec),
                       need_main_key_frame ? 1 : 0,
                       need_sub_key_frame ? 1 : 0);
        if (need_main_key_frame) {
            (void)media_service->RequestKeyFrame(StreamId::kMain,
                                                KeyFrameReason::kRecovery);
        }
        if (need_sub_key_frame) {
            (void)media_service->RequestKeyFrame(StreamId::kSub,
                                                KeyFrameReason::kRecovery);
        }
        return true;
    }

    void Stop() override {
        StopInternal();
    }

private:
    void StopInternal() {
        IMediaService *media_service = nullptr;
        infra::Executor *worker_executor = nullptr;
        FrameAttachId main_attach_id = 0;
        FrameAttachId sub_attach_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (run_state_ == MediaSourceRunState::kStopped) {
                return;
            }
            media_service = dependencies_.media_service;
            main_attach_id = main_attach_id_;
            sub_attach_id = sub_attach_id_;
            ResetRuntimeStateLocked();
            worker_executor = worker_executor_.get();
            run_state_ = MediaSourceRunState::kStopped;
        }
        if (media_service != nullptr) {
            if (main_attach_id != 0) {
                (void)media_service->DetachFrameSink(main_attach_id);
            }
            if (sub_attach_id != 0) {
                (void)media_service->DetachFrameSink(sub_attach_id);
            }
        }
        if (worker_executor != nullptr) {
            worker_executor->Stop(infra::StopMode::kDiscard);
        }
    }

public:
    bool IsHlsSupported(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr && source_state::IsHlsStreamReady(*stream);
    }

    bool IsFlvSupported(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr && stream->state == StreamState::kRunning &&
               source_state::IsFlvCodecSupported(stream->codec);
    }

    bool IsMjpegSupported(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr && stream->state == StreamState::kRunning &&
               source_state::IsMjpegCodecSupported(stream->codec);
    }

    bool IsStreamAvailable(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr && stream->state == StreamState::kRunning;
    }

    VideoCodec GetStreamCodec(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        if (stream != nullptr) {
            return stream->codec;
        }
        return VideoCodec::kH264;
    }

    MediaHlsPlaylist GetHlsPlaylist(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return MediaHlsPlaylist{};
        }
        stream->hls_requested = true;
        return source_state::BuildHlsPlaylist(*stream,
                                           options_.hls_segment_duration_ms,
                                           options_.hls_playlist_depth);
    }

    MediaSegmentRef GetHlsSegmentRef(StreamId stream_id,
                                      uint64_t sequence) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return MediaSegmentRef{};
        }
        stream->hls_requested = true;
        return source_state::FindHlsSegmentRef(*stream, sequence);
    }

    MediaFlvStartData GetFlvStartData(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return MediaFlvStartData{};
        }
        return source_state::BuildFlvStartData(*stream);
    }

    MediaSourceStatus GetBrowserStatus(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const source_state::StreamContext *stream = FindStream(stream_id);
        MediaSourceStatus status;
        if (stream == nullptr) {
            return status;
        }
        status.running = stream->state == StreamState::kRunning;
        status.hls_supported = source_state::IsHlsCodecSupported(stream->codec);
        status.flv_supported = source_state::IsFlvCodecSupported(stream->codec);
        status.mjpeg_supported =
            source_state::IsMjpegCodecSupported(stream->codec);
        status.browser_codec =
            status.hls_supported || status.flv_supported ||
            status.mjpeg_supported;
        status.hls_ready = source_state::IsHlsStreamReady(*stream);
        status.flv_ready = source_state::IsFlvStreamReady(*stream);
        status.mjpeg_ready = source_state::IsMjpegStreamReady(*stream);
        status.codec = stream->codec;
        status.hls_segment_count = static_cast<uint32_t>(
            stream->segments.size());
        status.flv_sequence_header_size =
            static_cast<uint32_t>(stream->sequence_header_tag.size());
        status.flv_last_keyframe_size =
            !stream->flv_gop_cache.complete || stream->flv_gop_cache.size == 0
                ? 0
                : static_cast<uint32_t>(
                      stream->flv_gop_cache.frames[stream->flv_gop_cache.head]
                          .total_size);
        status.hls_current_segment_size =
            stream->current_segment.body != nullptr
                ? stream->current_segment.body->size
                : 0;
        return status;
    }

    MediaFlvClientId
    AttachFlvClient(StreamId stream_id, uint64_t config_generation,
                    bool wait_for_keyframe, IMediaFlvSink *sink) override {
        if (sink == nullptr) {
            return 0;
        }
        IMediaService *media_service = nullptr;
        MediaFlvClientId client_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            source_state::StreamContext *stream = FindMutableStream(stream_id);
            if (stream == nullptr ||
                !source_state::IsBrowserStreamReady(stream->state,
                                                 stream->codec) ||
                !source_state::IsFlvCodecSupported(stream->codec) ||
                flv_clients_.Size() >= options_.max_flv_clients) {
                return 0;
            }
            client_id = flv_clients_.Attach(
                stream_id, config_generation, wait_for_keyframe, sink,
                options_.max_flv_clients);
            media_service = dependencies_.media_service;
        }
        if (media_service != nullptr) {
            (void)media_service->RequestKeyFrame(stream_id,
                                                KeyFrameReason::kNewClient);
        }
        return client_id;
    }

    bool DetachFlvClient(MediaFlvClientId client_id) override {
        std::lock_guard<std::mutex> guard(mutex_);
        return flv_clients_.Detach(client_id);
    }

    MediaMjpegClientId
    AttachMjpegClient(StreamId stream_id, IMediaMjpegSink *sink) override {
        if (sink == nullptr) {
            return 0;
        }
        MediaMjpegClientId client_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            source_state::StreamContext *stream = FindMutableStream(stream_id);
            if (stream == nullptr ||
                !source_state::IsMjpegStreamReady(*stream) ||
                mjpeg_clients_.Size() >= options_.max_mjpeg_clients) {
                return 0;
            }
            client_id = mjpeg_clients_.Attach(
                stream_id, sink, options_.max_mjpeg_clients);
        }
        return client_id;
    }

    bool DetachMjpegClient(MediaMjpegClientId client_id) override {
        std::lock_guard<std::mutex> guard(mutex_);
        return mjpeg_clients_.Detach(client_id);
    }

    FrameAttachId AttachFrameSink(
        const FrameAttachOptions &options, IFrameSink *sink) override {
        if (sink == nullptr || !IsStreamSupported(options.stream_id)) {
            return 0;
        }
        IMediaService *media_service = nullptr;
        FrameAttachId sink_id = 0;
        bool request_key_frame = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (run_state_ != MediaSourceRunState::kStarted ||
                FindStream(options.stream_id) == nullptr ||
                frame_dispatcher_.Size() >= options_.max_frame_sinks) {
                return 0;
            }
            sink_id = frame_dispatcher_.Attach(options, sink,
                                               options_.max_frame_sinks);
            media_service = dependencies_.media_service;
            request_key_frame = options.require_key_frame_first;
        }
        if (media_service != nullptr && request_key_frame) {
            (void)media_service->RequestKeyFrame(options.stream_id,
                                                KeyFrameReason::kNewClient);
        }
        return sink_id;
    }

    bool DetachFrameSink(FrameAttachId sink_id) override {
        std::lock_guard<std::mutex> guard(mutex_);
        return frame_dispatcher_.Detach(sink_id);
    }

    bool RequestKeyFrame(StreamId stream_id, KeyFrameReason reason) override {
        if (!IsStreamSupported(stream_id) ||
            dependencies_.media_service == nullptr) {
            return false;
        }
        return dependencies_.media_service->RequestKeyFrame(stream_id, reason);
    }

    MediaSourceStats GetStats() const override {
        std::lock_guard<std::mutex> guard(mutex_);
        MediaSourceStats stats = stats_;
        stats.enabled = run_state_ == MediaSourceRunState::kStarted;
        stats.active_flv_clients = static_cast<uint32_t>(flv_clients_.Size());
        stats.active_mjpeg_clients =
            static_cast<uint32_t>(mjpeg_clients_.Size());
        stats.active_frame_sinks =
            static_cast<uint32_t>(frame_dispatcher_.Size());
        return stats;
    }

    const char *Name() const override { return kServiceName; }

    void OnFrame(const FramePayload &input_frame) override {
        const EncodedFrame &frame = input_frame.encoded_frame;
        infra::Executor *worker_executor = nullptr;
        bool post_drain = false;
        if (!EncodedFrameHasPayload(&frame) ||
            !IsStreamSupported(frame.stream_id)) {
            return;
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (run_state_ != MediaSourceRunState::kStarted ||
                worker_executor_ == nullptr) {
                return;
            }
            PendingFrameQueue *queue = FindPendingQueue(frame.stream_id);
            if (queue == nullptr || !EnqueuePendingFrameLocked(queue, frame)) {
                return;
            }
            if (!drain_task_posted_) {
                drain_task_posted_ = true;
                worker_executor = worker_executor_.get();
                post_drain = true;
            }
        }
        if (post_drain && worker_executor != nullptr &&
            !worker_executor->Post([this]() { DrainPendingFrames(); })) {
            std::lock_guard<std::mutex> guard(mutex_);
            drain_task_posted_ = false;
        }
    }

    void OnSourceStateChanged(StreamId stream_id, StreamState state) override {
        std::lock_guard<std::mutex> guard(mutex_);
        source_state::StreamContext *stream = FindMutableStream(stream_id);
        if (stream != nullptr) {
            stream->state = state;
            INFRA_LOG_INFO(kServiceName, "source state stream=%s state=%d",
                           StreamName(stream_id), static_cast<int>(state));
        }
    }

private:
    enum class MediaSourceRunState {
        kStopped = 0,
        kStarting,
        kStarted,
    };

    void ResetRuntimeStateLocked() {
        main_attach_id_ = 0;
        sub_attach_id_ = 0;
        main_pending_.Clear();
        sub_pending_.Clear();
        drain_task_posted_ = false;
        last_drained_stream_ = StreamId::kSub;
        flv_clients_.Clear();
        mjpeg_clients_.Clear();
        frame_dispatcher_.Clear();
        source_state::ClearStreamContext(&main_stream_);
        source_state::ClearStreamContext(&sub_stream_);
    }

    void DrainPendingFrames() {
        while (true) {
            EncodedFrame frame;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (run_state_ != MediaSourceRunState::kStarted ||
                    !TakeNextPendingFrameLocked(&frame)) {
                    drain_task_posted_ = false;
                    return;
                }
            }
            source_state::ParsedFramePayload payload;
            if (!NormalizeFrameForDownstream(&frame)) {
                EncodedFrameUnref(&frame);
                continue;
            }
            const bool has_normalized_payload = BuildParsedFrame(frame, &payload);
            DispatchFrameSinks(payload);
            PackageBrowserFrame(payload, has_normalized_payload);
            source_state::ParsedFramePayloadUnref(&payload);
            EncodedFrameUnref(&frame);
        }
    }

    bool NormalizeFrameForDownstream(EncodedFrame *frame) {
        if (frame == nullptr) {
            return false;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        source_state::StreamContext *stream =
            FindMutableStream(frame->stream_id);
        if (stream == nullptr) {
            return false;
        }
        if (stream->codec != frame->codec) {
            source_state::ResetStream(stream, frame->codec);
        }
        if (!source_state::IsBrowserStreamReady(stream->state, stream->codec)) {
            return false;
        }
        return source_state::NormalizeFrameTimestamps(stream, frame);
    }

    bool BuildParsedFrame(const EncodedFrame &frame,
                          source_state::ParsedFramePayload *payload) {
        if (payload == nullptr) {
            return false;
        }
        source_state::ParseFramePayload(frame, payload);
        return source_state::HasParsedUnits(*payload);
    }

    void DispatchFrameSinks(const source_state::ParsedFramePayload &payload) {
        std::vector<source_clients::PendingMediaFrameSinkWrite> frame_sinks;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            source_state::StreamContext *stream =
                FindMutableStream(payload.encoded_frame.stream_id);
            if (stream == nullptr) {
                return;
            }
            if (stream->codec != payload.encoded_frame.codec) {
                source_state::ResetStream(stream, payload.encoded_frame.codec);
            }
            if (stream->state != StreamState::kRunning) {
                return;
            }
            frame_sinks = frame_dispatcher_.CollectWrites(payload.encoded_frame);
        }

        for (const source_clients::PendingMediaFrameSinkWrite &pending_sink :
             frame_sinks) {
            if (pending_sink.sink != nullptr) {
                pending_sink.sink->OnFrame(payload);
            }
        }
    }

    void PackageBrowserFrame(const source_state::ParsedFramePayload &payload,
                             bool has_payload) {
        if (!has_payload) {
            PackageMjpegFrame(payload);
            return;
        }
        const EncodedFrame &frame = payload.encoded_frame;
        std::vector<source_clients::PendingFlvClientWrite> clients;
        std::string sequence_header_tag;
        stream_mux::FlvVideoTagView flv_tag_view;
        bool has_flv_tag_view = false;
        bool package_hls = false;
        bool package_flv = false;
        bool update_flv_cache = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            source_state::StreamContext *stream = FindMutableStream(frame.stream_id);
            if (stream == nullptr) {
                return;
            }
            if (stream->codec != frame.codec) {
                source_state::ResetStream(stream, frame.codec);
            }
            if (!source_state::IsBrowserStreamReady(stream->state, stream->codec)) {
                return;
            }
            package_hls = stream->hls_requested;
            package_flv = flv_clients_.HasClient(frame.stream_id);
            update_flv_cache = source_state::IsFlvCodecSupported(stream->codec);
        }
        if (!package_hls && !package_flv && !update_flv_cache) {
            return;
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            source_state::StreamContext *stream = FindMutableStream(frame.stream_id);
            if (stream == nullptr) {
                return;
            }
            if (stream->codec != frame.codec) {
                source_state::ResetStream(stream, frame.codec);
            }
            if (!source_state::IsBrowserStreamReady(stream->state, stream->codec)) {
                return;
            }
            const bool was_hls_ready = source_state::IsHlsStreamReady(*stream);
            const bool was_flv_ready = source_state::IsFlvStreamReady(*stream);
            package_hls = stream->hls_requested;
            package_flv = flv_clients_.HasClient(frame.stream_id);
            update_flv_cache = source_state::IsFlvCodecSupported(stream->codec);

            source_state::PackagedFrameResult packaged_frame =
                source_state::AppendFrameToStream(
                    stream, frame, payload, package_hls,
                    package_flv || update_flv_cache,
                    options_.hls_segment_duration_ms,
                    HlsSegmentCacheDepth(options_));
            if (!packaged_frame.accepted) {
                return;
            }
            const bool hls_ready = source_state::IsHlsStreamReady(*stream);
            const bool flv_ready = source_state::IsFlvStreamReady(*stream);
            if ((!was_hls_ready && hls_ready) ||
                (!was_flv_ready && flv_ready)) {
                INFRA_LOG_INFO(
                    kServiceName,
                    "browser stream ready stream=%s hls=%d flv=%d "
                    "sequence_header=%zu cached_flv=%zu segments=%zu",
                    StreamName(frame.stream_id), hls_ready ? 1 : 0,
                    flv_ready ? 1 : 0, stream->sequence_header_tag.size(),
                    stream->flv_gop_cache.size,
                    stream->segments.size());
            }
            if (packaged_frame.hls_segment_created) {
                ++stats_.hls_segments_created;
            }
            flv_tag_view = packaged_frame.flv_tag_view;
            has_flv_tag_view = packaged_frame.has_flv_tag_view;
            const bool has_sequence_header =
                source_state::HasFlvSequenceHeader(*stream);
            clients = flv_clients_.CollectWrites(
                frame.stream_id, stream->config_generation,
                has_flv_tag_view, has_sequence_header,
                packaged_frame.keyframe);
            for (const source_clients::PendingFlvClientWrite &client : clients) {
                if (client.send_sequence_header) {
                    sequence_header_tag = stream->sequence_header_tag;
                    break;
                }
            }
        }

        WriteFlvClients(clients, sequence_header_tag, flv_tag_view,
                        has_flv_tag_view, frame, frame.stream_id);
    }

    void PackageMjpegFrame(const source_state::ParsedFramePayload &payload) {
        const EncodedFrame &frame = payload.encoded_frame;
        if (frame.codec != VideoCodec::kMjpeg ||
            !EncodedFrameHasPayload(&frame)) {
            return;
        }
        std::vector<source_clients::PendingMjpegClientWrite> clients;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            source_state::StreamContext *stream = FindMutableStream(frame.stream_id);
            if (stream == nullptr) {
                return;
            }
            if (stream->codec != frame.codec) {
                source_state::ResetStream(stream, frame.codec);
            }
            if (!source_state::IsMjpegStreamReady(*stream) ||
                !mjpeg_clients_.HasClient(frame.stream_id)) {
                return;
            }
            clients = mjpeg_clients_.CollectWrites(frame.stream_id);
        }
        WriteMjpegClients(clients, frame);
    }

    void WriteFlvClients(
        const std::vector<source_clients::PendingFlvClientWrite> &clients,
        const std::string &sequence_header_tag,
        const stream_mux::FlvVideoTagView &flv_tag_view,
        bool has_flv_tag_view,
        const EncodedFrame &frame,
        StreamId stream_id) {
        std::vector<MediaFlvClientId> detach_ids;
        for (const source_clients::PendingFlvClientWrite &client : clients) {
            if (client.starts_on_keyframe) {
                INFRA_LOG_INFO(kServiceName,
                               "HTTP-FLV client starts stream=%s client=%llu "
                               "sequence_header=%zu keyframe=%zu",
                               StreamName(stream_id),
                               static_cast<unsigned long long>(
                                   client.client_id),
                               sequence_header_tag.size(),
                               flv_tag_view.total_size);
            }
            if (client.send_sequence_header &&
                !client.sink->OnFlvChunk(
                    reinterpret_cast<const uint8_t *>(sequence_header_tag.data()),
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
            const bool sent_frame =
                client.sink->OnFlvVideoTag(flv_video_tag, frame);
            if (!sent_frame) {
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
        flv_clients_.ReleaseWrite(client_id);
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

    PendingFrameQueue *FindPendingQueue(StreamId stream_id) {
        if (stream_id == StreamId::kMain) {
            return &main_pending_;
        }
        if (stream_id == StreamId::kSub) {
            return &sub_pending_;
        }
        return nullptr;
    }

    bool EnqueuePendingFrameLocked(PendingFrameQueue *queue,
                                   const EncodedFrame &frame) {
        if (queue == nullptr) {
            return false;
        }
        if (queue->Full()) {
            if (stream_codec::IsKeyFrame(frame.frame_type)) {
                if (!queue->DropOldestNonKeyFrame() && !queue->Empty()) {
                    queue->PopFront();
                }
            } else if (!queue->DropOldestNonKeyFrame()) {
                return false;
            }
        }
        return queue->PushBack(frame);
    }

    bool TakeNextPendingFrameLocked(EncodedFrame *frame) {
        if (frame == nullptr) {
            return false;
        }
        const StreamId preferred_stream = last_drained_stream_ == StreamId::kMain
                                              ? StreamId::kSub
                                              : StreamId::kMain;
        const StreamId fallback_stream = preferred_stream == StreamId::kMain
                                             ? StreamId::kSub
                                             : StreamId::kMain;
        PendingFrameQueue *queue = FindPendingQueue(preferred_stream);
        StreamId selected_stream = preferred_stream;
        if (queue == nullptr || queue->Empty()) {
            queue = FindPendingQueue(fallback_stream);
            selected_stream = fallback_stream;
        }
        if (queue == nullptr || queue->Empty()) {
            return false;
        }
        if (!queue->TakeFront(frame)) {
            return false;
        }
        last_drained_stream_ = selected_stream;
        return true;
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

    MediaSourceServiceOptions options_;
    MediaSourceServiceDependencies dependencies_;
    std::unique_ptr<infra::Executor> worker_executor_;
    mutable std::mutex mutex_;
    PendingFrameQueue main_pending_;
    PendingFrameQueue sub_pending_;
    bool drain_task_posted_ = false;
    StreamId last_drained_stream_ = StreamId::kSub;
    source_state::StreamContext main_stream_;
    source_state::StreamContext sub_stream_;
    source_clients::FlvClientRegistry flv_clients_;
    source_clients::MjpegClientRegistry mjpeg_clients_;
    source_clients::MediaFrameDispatcher frame_dispatcher_;
    MediaSourceStats stats_;
    FrameAttachId main_attach_id_ = 0;
    FrameAttachId sub_attach_id_ = 0;
    MediaSourceRunState run_state_ = MediaSourceRunState::kStopped;
};

}  // namespace

std::unique_ptr<IMediaSourceService>
CreateMediaSourceService(const MediaSourceServiceOptions &options,
                         const MediaSourceServiceDependencies &dependencies) {
    return std::unique_ptr<IMediaSourceService>(
        new MediaSourceServiceImpl(options, dependencies));
}

}  // namespace live_stream
