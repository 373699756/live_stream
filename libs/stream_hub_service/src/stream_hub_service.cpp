#include "stream_hub_service.h"

#include "infra/executor.h"
#include "media/encoded_frame.h"
#include "media_service.h"
#include "stream_codec.h"
#include "stream_hub_stream_state.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
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

bool HasValidPayload(const EncodedFrame &frame) {
    return frame.buffer != nullptr && frame.size != 0 &&
           frame.offset <= frame.buffer->Size() &&
           frame.size <= frame.buffer->Size() - frame.offset;
}

class StreamHubServiceImpl : public IStreamHubService, public IFrameSink {
public:
    StreamHubServiceImpl(StreamHubServiceOptions options,
                         StreamHubServiceDependencies dependencies)
        : options_(std::move(options)), dependencies_(dependencies) {}

    ~StreamHubServiceImpl() override { Stop(); }

    bool Start() override {
        MediaService *media_service = nullptr;
        infra::Executor *worker_executor = nullptr;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (started_) {
                return true;
            }
            media_service = dependencies_.media_service;
            if (!worker_executor_) {
                worker_executor_.reset(new infra::Executor());
            }
            worker_executor = worker_executor_.get();
        }
        if (media_service == nullptr || worker_executor == nullptr) {
            return false;
        }
        if (options_.hls_segment_duration_ms == 0 ||
            options_.hls_playlist_depth == 0 || options_.max_flv_clients == 0) {
            return false;
        }
        infra::ExecutorOptions executor_options;
        executor_options.worker_count = kWorkerThreadCount;
        executor_options.queue_capacity = kWorkerQueueCapacity;
        if (!worker_executor->Start(executor_options)) {
            return false;
        }
        const VideoCodec main_codec = media_service->GetStreamCodec(StreamId::kMain);
        const VideoCodec sub_codec = media_service->GetStreamCodec(StreamId::kSub);
        FrameSubscribeOptions main_options;
        main_options.stream_id = StreamId::kMain;
        main_options.require_key_frame_first = true;
        main_options.sink_name = kServiceName;
        const FrameSubscriptionId main_subscription_id =
            media_service->SubscribeFrames(main_options, this);

        FrameSubscribeOptions sub_options;
        sub_options.stream_id = StreamId::kSub;
        sub_options.require_key_frame_first = true;
        sub_options.sink_name = kServiceName;
        const FrameSubscriptionId sub_subscription_id =
            media_service->SubscribeFrames(sub_options, this);
        const bool subscribed =
            main_subscription_id != 0 || sub_subscription_id != 0;
        if (!subscribed) {
            worker_executor->Stop(infra::StopMode::kDiscard);
            return false;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        main_subscription_id_ = main_subscription_id;
        sub_subscription_id_ = sub_subscription_id;
        main_pending_ = StreamFrameQueue{};
        sub_pending_ = StreamFrameQueue{};
        main_stream_ = hub_state::StreamContext{};
        main_stream_.codec = main_codec;
        main_stream_.state =
            main_subscription_id != 0 ? StreamState::kRunning : StreamState::kClosed;
        sub_stream_ = hub_state::StreamContext{};
        sub_stream_.codec = sub_codec;
        sub_stream_.state =
            sub_subscription_id != 0 ? StreamState::kRunning : StreamState::kClosed;
        drain_task_posted_ = false;
        last_drained_stream_ = StreamId::kSub;
        started_ = true;
        return true;
    }

    void Stop() override {
        MediaService *media_service = nullptr;
        infra::Executor *worker_executor = nullptr;
        FrameSubscriptionId main_subscription_id = 0;
        FrameSubscriptionId sub_subscription_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!started_) {
                return;
            }
            media_service = dependencies_.media_service;
            main_subscription_id = main_subscription_id_;
            sub_subscription_id = sub_subscription_id_;
            main_subscription_id_ = 0;
            sub_subscription_id_ = 0;
            main_pending_ = StreamFrameQueue{};
            sub_pending_ = StreamFrameQueue{};
            drain_task_posted_ = false;
            flv_clients_.clear();
            main_stream_ = hub_state::StreamContext{};
            sub_stream_ = hub_state::StreamContext{};
            worker_executor = worker_executor_.get();
            started_ = false;
        }
        if (media_service != nullptr) {
            if (main_subscription_id != 0) {
                (void)media_service->UnsubscribeFrames(main_subscription_id);
            }
            if (sub_subscription_id != 0) {
                (void)media_service->UnsubscribeFrames(sub_subscription_id);
            }
        }
        if (worker_executor != nullptr) {
            worker_executor->Stop(infra::StopMode::kDiscard);
        }
    }

    bool IsHlsSupported(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const hub_state::StreamContext *stream = FindStream(stream_id);
        return stream != nullptr &&
               hub_state::IsBrowserStreamReady(stream->state, stream->codec);
    }

    bool IsFlvSupported(StreamId stream_id) const override {
        return IsHlsSupported(stream_id);
    }

    StreamHlsPlaylist GetHlsPlaylist(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const hub_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return StreamHlsPlaylist{};
        }
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
        return hub_state::FindHlsSegment(*stream, sequence);
    }

    StreamFlvBootstrap GetFlvBootstrap(StreamId stream_id) const override {
        std::lock_guard<std::mutex> guard(mutex_);
        const hub_state::StreamContext *stream = FindStream(stream_id);
        if (stream == nullptr) {
            return StreamFlvBootstrap{};
        }
        return hub_state::BuildFlvBootstrap(*stream);
    }

    StreamFlvClientId
    AttachFlvClient(StreamId stream_id, uint64_t config_generation,
                    const std::shared_ptr<IStreamFlvSink> &sink) override {
        if (sink == nullptr) {
            return 0;
        }
        MediaService *media_service = nullptr;
        StreamFlvClientId client_id = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            hub_state::StreamContext *stream = FindMutableStream(stream_id);
            if (stream == nullptr ||
                !hub_state::IsBrowserStreamReady(stream->state, stream->codec) ||
                flv_clients_.size() >= options_.max_flv_clients) {
                return 0;
            }
            client_id = next_flv_client_id_++;
            FlvClientState client;
            client.stream_id = stream_id;
            client.config_generation = config_generation;
            client.sink = sink;
            flv_clients_[client_id] = client;
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
        return flv_clients_.erase(client_id) != 0;
    }

    StreamHubServiceStats GetStats() const override {
        std::lock_guard<std::mutex> guard(mutex_);
        StreamHubServiceStats stats = stats_;
        stats.enabled = started_;
        stats.active_flv_clients = static_cast<uint32_t>(flv_clients_.size());
        return stats;
    }

    const char *Name() const override { return kServiceName; }

    void OnFrame(const EncodedFrame &frame) override {
        infra::Executor *worker_executor = nullptr;
        bool post_drain = false;
        if (!HasValidPayload(frame) || !IsStreamSupported(frame.stream_id)) {
            return;
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!started_ || worker_executor_ == nullptr) {
                return;
            }
            StreamFrameQueue *queue = FindPendingQueue(frame.stream_id);
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
        }
    }

private:
    struct PendingFlvClientWrite {
        StreamFlvClientId client_id = 0;
        std::shared_ptr<IStreamFlvSink> sink;
        bool send_sequence_header = false;
    };

    struct StreamFrameQueue {
        std::deque<EncodedFrame> frames;
    };

    void DrainPendingFrames() {
        while (true) {
            EncodedFrame frame;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (!started_ || !TakeNextPendingFrameLocked(&frame)) {
                    drain_task_posted_ = false;
                    return;
                }
            }
            ProcessFrame(frame);
        }
    }

    void ProcessFrame(const EncodedFrame &frame) {
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
        }

        // 码流进入 hub 时仍是编码器输出的 Annex-B。先在锁外解析 NAL，避免在
        // 互斥锁内做线性扫描，后面同一批 NAL 会同时生成 HLS 和 FLV 两种载荷。
        const hub_state::ParsedFramePayload payload =
            hub_state::ParseFramePayload(frame);
        if (!hub_state::HasParsedUnits(payload)) {
            return;
        }

        std::vector<StreamFlvClientId> detach_ids;
        std::vector<PendingFlvClientWrite> clients;
        std::string sequence_header_tag;
        std::string flv_tag;
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

            const hub_state::PackagedFrameResult packaged_frame =
                hub_state::AppendFrameToStream(
                    stream, frame, payload, options_.hls_segment_duration_ms,
                    options_.hls_playlist_depth);
            if (!packaged_frame.accepted) {
                return;
            }
            if (packaged_frame.hls_segment_created) {
                ++stats_.hls_segments_created;
            }
            sequence_header_tag = packaged_frame.sequence_header_tag;
            flv_tag = packaged_frame.flv_tag;
            for (auto &item : flv_clients_) {
                if (item.second.stream_id != frame.stream_id ||
                    item.second.sink == nullptr || flv_tag.empty()) {
                    continue;
                }
                const bool needs_config =
                    item.second.config_generation != stream->config_generation &&
                    !sequence_header_tag.empty();
                if (needs_config) {
                    item.second.config_generation = stream->config_generation;
                }
                PendingFlvClientWrite client;
                client.client_id = item.first;
                client.sink = item.second.sink;
                client.send_sequence_header = needs_config;
                clients.push_back(std::move(client));
            }
        }

        for (const PendingFlvClientWrite &client : clients) {
            if (client.send_sequence_header &&
                !client.sink->OnFlvChunk(
                    reinterpret_cast<const uint8_t *>(sequence_header_tag.data()),
                    sequence_header_tag.size())) {
                detach_ids.push_back(client.client_id);
                continue;
            }
            if (!client.sink->OnFlvChunk(
                    reinterpret_cast<const uint8_t *>(flv_tag.data()),
                    flv_tag.size())) {
                detach_ids.push_back(client.client_id);
            }
        }
        for (StreamFlvClientId client_id : detach_ids) {
            if (client_id != 0) {
                (void)DetachFlvClient(client_id);
            }
        }
    }
    struct FlvClientState {
        StreamId stream_id = StreamId::kMain;
        uint64_t config_generation = 0;
        std::shared_ptr<IStreamFlvSink> sink;
    };

    StreamFrameQueue *FindPendingQueue(StreamId stream_id) {
        if (stream_id == StreamId::kMain) {
            return &main_pending_;
        }
        if (stream_id == StreamId::kSub) {
            return &sub_pending_;
        }
        return nullptr;
    }

    bool DropOldestNonKeyFrameLocked(StreamFrameQueue *queue) {
        if (queue == nullptr) {
            return false;
        }
        for (auto it = queue->frames.begin(); it != queue->frames.end(); ++it) {
            if (!stream_codec::IsKeyFrame(it->frame_type)) {
                queue->frames.erase(it);
                return true;
            }
        }
        return false;
    }

    bool EnqueuePendingFrameLocked(StreamFrameQueue *queue,
                                   const EncodedFrame &frame) {
        if (queue == nullptr) {
            return false;
        }
        if (queue->frames.size() >= kMaxPendingFramesPerStream) {
            if (stream_codec::IsKeyFrame(frame.frame_type)) {
                if (!DropOldestNonKeyFrameLocked(queue) && !queue->frames.empty()) {
                    queue->frames.pop_front();
                }
            } else if (!DropOldestNonKeyFrameLocked(queue)) {
                return false;
            }
        }
        queue->frames.push_back(frame);
        return true;
    }

    bool PopPendingFrameLocked(StreamId stream_id, EncodedFrame *frame) {
        StreamFrameQueue *queue = FindPendingQueue(stream_id);
        if (queue == nullptr || frame == nullptr || queue->frames.empty()) {
            return false;
        }
        *frame = queue->frames.front();
        queue->frames.pop_front();
        last_drained_stream_ = stream_id;
        return true;
    }

    bool TakeNextPendingFrameLocked(EncodedFrame *frame) {
        const StreamId preferred_stream = last_drained_stream_ == StreamId::kMain
                                              ? StreamId::kSub
                                              : StreamId::kMain;
        if (PopPendingFrameLocked(preferred_stream, frame)) {
            return true;
        }
        const StreamId fallback_stream = preferred_stream == StreamId::kMain
                                             ? StreamId::kSub
                                             : StreamId::kMain;
        return PopPendingFrameLocked(fallback_stream, frame);
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
    StreamFrameQueue main_pending_;
    StreamFrameQueue sub_pending_;
    bool drain_task_posted_ = false;
    StreamId last_drained_stream_ = StreamId::kSub;
    hub_state::StreamContext main_stream_;
    hub_state::StreamContext sub_stream_;
    std::map<StreamFlvClientId, FlvClientState> flv_clients_;
    StreamHubServiceStats stats_;
    FrameSubscriptionId main_subscription_id_ = 0;
    FrameSubscriptionId sub_subscription_id_ = 0;
    StreamFlvClientId next_flv_client_id_ = 1;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<IStreamHubService>
CreateStreamHubService(const StreamHubServiceOptions &options,
                       const StreamHubServiceDependencies &dependencies) {
    return std::unique_ptr<IStreamHubService>(
        new StreamHubServiceImpl(options, dependencies));
}

}  // namespace live_stream
