#include "stream_hub_service.h"

#include "flv_client_registry.h"
#include "infra/executor.h"
#include "infra/log.h"
#include "media/encoded_frame.h"
#include "stream_frame_dispatcher.h"
#include "stream_codec.h"
#include "stream_hub_stream_state.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace hub_state = stream_hub_internal;
namespace {

constexpr const char *kServiceName = "stream_hub_service";
constexpr uint32_t kWorkerQueueCapacity = 4;
constexpr uint32_t kWorkerThreadCount = 1;
constexpr size_t kMaxPendingFramesPerStream = 4;

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

class StreamHubServiceImpl : public IStreamHubService, public IFrameSink {
public:
    StreamHubServiceImpl(StreamHubServiceOptions options,
                         StreamHubServiceDependencies dependencies)
        : options_(std::move(options)), dependencies_(dependencies) {}

    ~StreamHubServiceImpl() override { StopInternal(); }

    bool Start() override {
        IMediaService *media_service = nullptr;
        infra::Executor *worker_executor = nullptr;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (run_state_ == StreamHubRunState::kStarted) {
                return true;
            }
            if (run_state_ == StreamHubRunState::kStarting) {
                return false;
            }
            media_service = dependencies_.media_service;
            if (!worker_executor_) {
                worker_executor_.reset(new infra::Executor());
            }
            worker_executor = worker_executor_.get();
            run_state_ = StreamHubRunState::kStarting;
        }
        if (media_service == nullptr || worker_executor == nullptr) {
            std::lock_guard<std::mutex> guard(mutex_);
            ResetRuntimeStateLocked();
            run_state_ = StreamHubRunState::kStopped;
            return false;
        }
        if (options_.hls_segment_duration_ms == 0 ||
            options_.hls_playlist_depth == 0 || options_.max_flv_clients == 0 ||
            options_.max_frame_sinks == 0) {
            std::lock_guard<std::mutex> guard(mutex_);
            ResetRuntimeStateLocked();
            run_state_ = StreamHubRunState::kStopped;
            return false;
        }
        infra::ExecutorOptions executor_options;
        executor_options.worker_count = kWorkerThreadCount;
        executor_options.queue_capacity = kWorkerQueueCapacity;
        if (!worker_executor->Start(executor_options)) {
            std::lock_guard<std::mutex> guard(mutex_);
            ResetRuntimeStateLocked();
            run_state_ = StreamHubRunState::kStopped;
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
                run_state_ = StreamHubRunState::kStopped;
            }
            worker_executor->Stop(infra::StopMode::kDiscard);
            return false;
        }
        bool need_main_key_frame = false;
        bool need_sub_key_frame = false;
        bool start_was_cancelled = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (run_state_ != StreamHubRunState::kStarting) {
                start_was_cancelled = true;
            } else {
                main_attach_id_ = main_attach_id;
                sub_attach_id_ = sub_attach_id;
                run_state_ = StreamHubRunState::kStarted;
                need_main_key_frame = main_attach_id_ != 0;
                need_sub_key_frame = sub_attach_id_ != 0;
            }
        }
        if (start_was_cancelled) {
            {
                std::lock_guard<std::mutex> guard(mutex_);
                ResetRuntimeStateLocked();
                run_state_ = StreamHubRunState::kStopped;
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
                       "stream hub attached main_attach_id=%llu sub_attach_id=%llu "
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
            if (run_state_ == StreamHubRunState::kStopped) {
                return;
            }
            media_service = dependencies_.media_service;
            main_attach_id = main_attach_id_;
            sub_attach_id = sub_attach_id_;
            ResetRuntimeStateLocked();
            worker_executor = worker_executor_.get();
            run_state_ = StreamHubRunState::kStopped;
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
        const hub_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr && hub_state::IsHlsStreamReady(*stream);
    }

    bool IsFlvSupported(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const hub_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr && stream->state == StreamState::kRunning &&
               hub_state::IsFlvCodecSupported(stream->codec);
    }

    bool IsStreamAvailable(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const hub_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr && stream->state == StreamState::kRunning;
    }

    VideoCodec GetStreamCodec(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const hub_state::StreamContext *stream = FindStream(stream_id);
        if (stream != nullptr) {
            return stream->codec;
        }
        return VideoCodec::kH264;
    }

    StreamHlsPlaylist GetHlsPlaylist(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const hub_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return StreamHlsPlaylist{};
        }
        stream->hls_requested = true;
        return hub_state::BuildHlsPlaylist(*stream,
                                           options_.hls_segment_duration_ms);
    }

    StreamSegment GetHlsSegment(StreamId stream_id,
                                uint64_t sequence) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const hub_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return StreamSegment{};
        }
        stream->hls_requested = true;
        return hub_state::FindHlsSegment(*stream, sequence);
    }

    StreamFlvStartData GetFlvStartData(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const hub_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return StreamFlvStartData{};
        }
        return hub_state::BuildFlvStartData(*stream);
    }

    StreamBrowserStatus GetBrowserStatus(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const hub_state::StreamContext *stream = FindStream(stream_id);
        StreamBrowserStatus status;
        if (stream == nullptr) {
            return status;
        }
        status.running = stream->state == StreamState::kRunning;
        status.hls_supported = hub_state::IsHlsCodecSupported(stream->codec);
        status.flv_supported = hub_state::IsFlvCodecSupported(stream->codec);
        status.browser_codec = status.hls_supported;
        status.hls_ready = hub_state::IsHlsStreamReady(*stream);
        status.flv_ready = hub_state::IsFlvStreamReady(*stream);
        status.codec = stream->codec;
        status.hls_segment_count =
            static_cast<uint32_t>(stream->segments.size());
        status.flv_sequence_header_size =
            static_cast<uint32_t>(stream->sequence_header_tag.size());
        status.flv_last_keyframe_size =
            static_cast<uint32_t>(stream->last_keyframe_tag.size());
        status.hls_current_segment_size =
            static_cast<uint32_t>(stream->current_segment.body.size());
        return status;
    }

    StreamFlvClientId
    AttachFlvClient(StreamId stream_id, uint64_t config_generation,
                    bool wait_for_keyframe, IStreamFlvSink *sink) override {
        if (sink == nullptr) {
            return 0;
        }
        IMediaService *media_service = nullptr;
        StreamFlvClientId client_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            hub_state::StreamContext *stream = FindMutableStream(stream_id);
            if (stream == nullptr ||
                !hub_state::IsBrowserStreamReady(stream->state,
                                                 stream->codec) ||
                !hub_state::IsFlvCodecSupported(stream->codec) ||
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

    bool DetachFlvClient(StreamFlvClientId client_id) override {
        std::lock_guard<std::mutex> guard(mutex_);
        return flv_clients_.Detach(client_id);
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
            if (run_state_ != StreamHubRunState::kStarted ||
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

    StreamHubServiceStats GetStats() const override {
        std::lock_guard<std::mutex> guard(mutex_);
        StreamHubServiceStats stats = stats_;
        stats.enabled = run_state_ == StreamHubRunState::kStarted;
        stats.active_flv_clients = static_cast<uint32_t>(flv_clients_.Size());
        stats.active_frame_sinks =
            static_cast<uint32_t>(frame_dispatcher_.Size());
        return stats;
    }

    const char *Name() const override { return kServiceName; }

    void OnFrame(const FramePayload &input_frame) override {
        const EncodedFrame &frame = input_frame.encoded_frame;
        infra::Executor *worker_executor = nullptr;
        bool post_drain = false;
        if (!frame.HasValidPayload() || !IsStreamSupported(frame.stream_id)) {
            return;
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (run_state_ != StreamHubRunState::kStarted ||
                worker_executor_ == nullptr) {
                return;
            }
            std::deque<EncodedFrame> *queue =
                FindPendingQueue(frame.stream_id);
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
        hub_state::StreamContext *stream = FindMutableStream(stream_id);
        if (stream != nullptr) {
            stream->state = state;
            INFRA_LOG_INFO(kServiceName, "source state stream=%s state=%d",
                           StreamName(stream_id), static_cast<int>(state));
        }
    }

private:
    enum class StreamHubRunState {
        kStopped = 0,
        kStarting,
        kStarted,
    };

    void ResetRuntimeStateLocked() {
        main_attach_id_ = 0;
        sub_attach_id_ = 0;
        main_pending_.clear();
        sub_pending_.clear();
        drain_task_posted_ = false;
        last_drained_stream_ = StreamId::kSub;
        flv_clients_.Clear();
        frame_dispatcher_.Clear();
        main_stream_ = hub_state::StreamContext{};
        sub_stream_ = hub_state::StreamContext{};
    }

    void DrainPendingFrames() {
        while (true) {
            EncodedFrame frame;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (run_state_ != StreamHubRunState::kStarted ||
                    !TakeNextPendingFrameLocked(&frame)) {
                    drain_task_posted_ = false;
                    return;
                }
            }
            hub_state::ParsedFramePayload payload;
            const bool has_payload = BuildParsedFrame(frame, &payload);
            DispatchFrameSinks(payload);
            PackageBrowserFrame(payload, has_payload);
        }
    }

    bool BuildParsedFrame(const EncodedFrame &frame,
                          hub_state::ParsedFramePayload *payload) {
        if (payload == nullptr) {
            return false;
        }
        hub_state::ParseFramePayload(frame, payload);
        return hub_state::HasParsedUnits(*payload);
    }

    void DispatchFrameSinks(const hub_state::ParsedFramePayload &payload) {
        std::vector<hub_state::PendingStreamFrameSinkWrite> frame_sinks;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            hub_state::StreamContext *stream =
                FindMutableStream(payload.encoded_frame.stream_id);
            if (stream == nullptr) {
                return;
            }
            if (stream->codec != payload.encoded_frame.codec) {
                hub_state::ResetStream(stream, payload.encoded_frame.codec);
            }
            if (stream->state != StreamState::kRunning) {
                return;
            }
            frame_sinks = frame_dispatcher_.CollectWrites(payload.encoded_frame);
        }

        for (const hub_state::PendingStreamFrameSinkWrite &pending_sink :
             frame_sinks) {
            if (pending_sink.sink != nullptr) {
                pending_sink.sink->OnFrame(payload);
            }
        }
    }

    void PackageBrowserFrame(const hub_state::ParsedFramePayload &payload,
                             bool has_payload) {
        if (!has_payload) {
            return;
        }
        const EncodedFrame &frame = payload.encoded_frame;
        std::vector<hub_state::PendingFlvClientWrite> clients;
        std::string sequence_header_tag;
        std::string flv_tag;
        bool package_hls = false;
        bool package_flv = false;
        bool update_flv_header = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            hub_state::StreamContext *stream = FindMutableStream(frame.stream_id);
            if (stream == nullptr) {
                return;
            }
            if (stream->codec != frame.codec) {
                hub_state::ResetStream(stream, frame.codec);
            }
            if (!hub_state::IsBrowserStreamReady(stream->state, stream->codec)) {
                return;
            }
            package_hls = stream->hls_requested;
            package_flv = flv_clients_.HasClient(frame.stream_id);
            update_flv_header = hub_state::IsFlvCodecSupported(stream->codec);
        }
        if (!package_hls && !package_flv && !update_flv_header) {
            return;
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            hub_state::StreamContext *stream = FindMutableStream(frame.stream_id);
            if (stream == nullptr) {
                return;
            }
            if (stream->codec != frame.codec) {
                hub_state::ResetStream(stream, frame.codec);
            }
            if (!hub_state::IsBrowserStreamReady(stream->state, stream->codec)) {
                return;
            }
            const bool was_hls_ready = hub_state::IsHlsStreamReady(*stream);
            const bool was_flv_ready = hub_state::IsFlvStreamReady(*stream);
            package_hls = stream->hls_requested;
            package_flv = flv_clients_.HasClient(frame.stream_id);

            hub_state::PackagedFrameResult packaged_frame =
                hub_state::AppendFrameToStream(
                    stream, frame, payload, package_hls, package_flv,
                    options_.hls_segment_duration_ms,
                    options_.hls_playlist_depth);
            if (!packaged_frame.accepted) {
                return;
            }
            const bool hls_ready = hub_state::IsHlsStreamReady(*stream);
            const bool flv_ready = hub_state::IsFlvStreamReady(*stream);
            if ((!was_hls_ready && hls_ready) ||
                (!was_flv_ready && flv_ready)) {
                INFRA_LOG_INFO(
                    kServiceName,
                    "browser stream ready stream=%s hls=%d flv=%d "
                    "sequence_header=%zu last_keyframe=%zu segments=%zu",
                    StreamName(frame.stream_id), hls_ready ? 1 : 0,
                    flv_ready ? 1 : 0, stream->sequence_header_tag.size(),
                    stream->last_keyframe_tag.size(), stream->segments.size());
            }
            if (packaged_frame.hls_segment_created) {
                ++stats_.hls_segments_created;
            } else if (packaged_frame.hls_segment_updated) {
                ++stats_.hls_segments_created;
            }
            flv_tag = std::move(packaged_frame.flv_tag);
            const bool has_sequence_header =
                hub_state::HasFlvSequenceHeader(*stream);
            clients = flv_clients_.CollectWrites(
                frame.stream_id, stream->config_generation, !flv_tag.empty(),
                has_sequence_header, packaged_frame.keyframe);
            for (const hub_state::PendingFlvClientWrite &client : clients) {
                if (client.send_sequence_header) {
                    sequence_header_tag = stream->sequence_header_tag;
                    break;
                }
            }
        }

        WriteFlvClients(clients, sequence_header_tag, flv_tag, frame.stream_id);
    }

    void WriteFlvClients(
        const std::vector<hub_state::PendingFlvClientWrite> &clients,
        const std::string &sequence_header_tag,
        const std::string &flv_tag,
        StreamId stream_id) {
        std::vector<StreamFlvClientId> detach_ids;
        for (const hub_state::PendingFlvClientWrite &client : clients) {
            if (client.starts_on_keyframe) {
                INFRA_LOG_INFO(kServiceName,
                               "HTTP-FLV client starts stream=%s client=%llu "
                               "sequence_header=%zu keyframe=%zu",
                               StreamName(stream_id),
                               static_cast<unsigned long long>(
                                   client.client_id),
                               sequence_header_tag.size(), flv_tag.size());
            }
            if (client.send_sequence_header &&
                !client.sink->OnFlvChunk(
                    reinterpret_cast<const uint8_t *>(sequence_header_tag.data()),
                    sequence_header_tag.size())) {
                detach_ids.push_back(client.client_id);
                ReleaseFlvClientWrite(client.client_id);
                continue;
            }
            if (!client.sink->OnFlvChunk(
                    reinterpret_cast<const uint8_t *>(flv_tag.data()),
                    flv_tag.size())) {
                detach_ids.push_back(client.client_id);
            }
            ReleaseFlvClientWrite(client.client_id);
        }
        for (StreamFlvClientId client_id : detach_ids) {
            if (client_id != 0) {
                (void)DetachFlvClient(client_id);
            }
        }
    }

    void ReleaseFlvClientWrite(StreamFlvClientId client_id) {
        std::lock_guard<std::mutex> guard(mutex_);
        flv_clients_.ReleaseWrite(client_id);
    }

    std::deque<EncodedFrame> *FindPendingQueue(StreamId stream_id) {
        if (stream_id == StreamId::kMain) {
            return &main_pending_;
        }
        if (stream_id == StreamId::kSub) {
            return &sub_pending_;
        }
        return nullptr;
    }

    bool DropOldestNonKeyFrameLocked(std::deque<EncodedFrame> *queue) {
        if (queue == nullptr) {
            return false;
        }
        for (auto it = queue->begin(); it != queue->end(); ++it) {
            if (!stream_codec::IsKeyFrame(it->frame_type)) {
                queue->erase(it);
                return true;
            }
        }
        return false;
    }

    bool EnqueuePendingFrameLocked(std::deque<EncodedFrame> *queue,
                                   const EncodedFrame &frame) {
        if (queue == nullptr) {
            return false;
        }
        if (queue->size() >= kMaxPendingFramesPerStream) {
            if (stream_codec::IsKeyFrame(frame.frame_type)) {
                if (!DropOldestNonKeyFrameLocked(queue) && !queue->empty()) {
                    queue->pop_front();
                }
            } else if (!DropOldestNonKeyFrameLocked(queue)) {
                return false;
            }
        }
        queue->push_back(frame);
        return true;
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
        std::deque<EncodedFrame> *queue = FindPendingQueue(preferred_stream);
        StreamId selected_stream = preferred_stream;
        if (queue == nullptr || queue->empty()) {
            queue = FindPendingQueue(fallback_stream);
            selected_stream = fallback_stream;
        }
        if (queue == nullptr || queue->empty()) {
            return false;
        }
        *frame = queue->front();
        queue->pop_front();
        last_drained_stream_ = selected_stream;
        return true;
    }

    const hub_state::StreamContext *FindStream(StreamId stream_id) const {
        if (stream_id == StreamId::kMain) {
            return &main_stream_;
        }
        if (stream_id == StreamId::kSub) {
            return &sub_stream_;
        }
        return nullptr;
    }

    hub_state::StreamContext *FindMutableStream(StreamId stream_id) {
        if (stream_id == StreamId::kMain) {
            return &main_stream_;
        }
        if (stream_id == StreamId::kSub) {
            return &sub_stream_;
        }
        return nullptr;
    }

    StreamHubServiceOptions options_;
    StreamHubServiceDependencies dependencies_;
    std::unique_ptr<infra::Executor> worker_executor_;
    mutable std::mutex mutex_;
    std::deque<EncodedFrame> main_pending_;
    std::deque<EncodedFrame> sub_pending_;
    bool drain_task_posted_ = false;
    StreamId last_drained_stream_ = StreamId::kSub;
    hub_state::StreamContext main_stream_;
    hub_state::StreamContext sub_stream_;
    hub_state::FlvClientRegistry flv_clients_;
    hub_state::StreamFrameDispatcher frame_dispatcher_;
    StreamHubServiceStats stats_;
    FrameAttachId main_attach_id_ = 0;
    FrameAttachId sub_attach_id_ = 0;
    StreamHubRunState run_state_ = StreamHubRunState::kStopped;
};

}  // namespace

std::unique_ptr<IStreamHubService>
CreateStreamHubService(const StreamHubServiceOptions &options,
                       const StreamHubServiceDependencies &dependencies) {
    return std::unique_ptr<IStreamHubService>(
        new StreamHubServiceImpl(options, dependencies));
}

}  // namespace live_stream
