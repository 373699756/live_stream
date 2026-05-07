#include "stream_hub_service.h"

#include "infra/executor.h"
#include "media/encoded_frame.h"
#include "media_service.h"
#include "stream_codec.h"
#include "stream_mux.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char *kServiceName = "stream_hub_service";
constexpr uint32_t kWorkerQueueCapacity = 4;
constexpr uint32_t kWorkerThreadCount = 1;
constexpr size_t kMaxPendingFramesPerStream = 4;

struct HlsSegmentState {
  bool started = false;
  uint64_t sequence = 0;
  int64_t start_pts_us = 0;
  int64_t last_pts_us = 0;
  std::string body;
};

bool IsStreamSupported(StreamId stream_id) {
  return stream_id == StreamId::kMain || stream_id == StreamId::kSub;
}

bool IsBrowserCodec(VideoCodec codec) { return codec == VideoCodec::kH264; }

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
      main_stream_ = StreamContext{};
      sub_stream_ = StreamContext{};
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
    const StreamContext *stream = FindStream(stream_id);
    return stream != nullptr && IsBrowserCodec(stream->codec);
  }

  bool IsFlvSupported(StreamId stream_id) const override {
    return IsHlsSupported(stream_id);
  }

  StreamHlsPlaylist GetHlsPlaylist(StreamId stream_id) const override {
    std::lock_guard<std::mutex> guard(mutex_);
    StreamHlsPlaylist playlist;
    const StreamContext *stream = FindStream(stream_id);
    if (stream == nullptr || !IsBrowserCodec(stream->codec) ||
        stream->segments.empty()) {
      return playlist;
    }
    playlist.supported = true;
    playlist.media_sequence = stream->segments.front().sequence;
    int64_t max_duration_us =
        static_cast<int64_t>(options_.hls_segment_duration_ms) * 1000;
    for (const StreamSegment &segment : stream->segments) {
      playlist.entries.push_back(
          StreamHlsEntry{segment.sequence, segment.duration_us});
      max_duration_us = std::max(max_duration_us, segment.duration_us);
    }
    playlist.target_duration_sec = static_cast<uint32_t>(
        std::max<int64_t>(1, (max_duration_us + 999999) / 1000000));
    return playlist;
  }

  StreamSegment GetHlsSegment(StreamId stream_id,
                             uint64_t sequence) const override {
    std::lock_guard<std::mutex> guard(mutex_);
    const StreamContext *stream = FindStream(stream_id);
    if (stream == nullptr || !IsBrowserCodec(stream->codec)) {
      return StreamSegment{};
    }
    for (const StreamSegment &segment : stream->segments) {
      if (segment.sequence == sequence) {
        return segment;
      }
    }
    return StreamSegment{};
  }

  StreamFlvBootstrap GetFlvBootstrap(StreamId stream_id) const override {
    std::lock_guard<std::mutex> guard(mutex_);
    StreamFlvBootstrap bootstrap;
    const StreamContext *stream = FindStream(stream_id);
    if (stream == nullptr || !IsBrowserCodec(stream->codec)) {
      return bootstrap;
    }
    bootstrap.supported = true;
    bootstrap.file_header = stream_mux::BuildFlvFileHeader();
    bootstrap.sequence_header = stream->sequence_header_tag;
    bootstrap.last_keyframe = stream->last_keyframe_tag;
    bootstrap.config_generation = stream->config_generation;
    return bootstrap;
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
      StreamContext *stream = FindMutableStream(stream_id);
      if (stream == nullptr || !IsBrowserCodec(stream->codec) ||
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
    StreamContext *stream = FindMutableStream(stream_id);
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
    const uint8_t *payload = frame.buffer->Data() + frame.offset;
    const size_t size = frame.size;
    const std::vector<stream_codec::H264NalUnit> units =
        stream_codec::ParseH264AnnexBNalUnits(payload, size);
    if (units.empty()) {
      return;
    }

    std::vector<StreamFlvClientId> detach_ids;
    std::vector<PendingFlvClientWrite> clients;
    std::string sequence_header_tag;
    std::string flv_tag;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      StreamContext *stream = FindMutableStream(frame.stream_id);
      if (stream == nullptr) {
        return;
      }
      if (stream->codec != frame.codec) {
        ResetStream(stream, frame.codec);
      }
      if (!IsBrowserCodec(stream->codec)) {
        return;
      }

      bool has_sps = false;
      bool has_pps = false;
      stream_codec::ExtractH264ParameterSets(units, &stream->sps, &stream->pps,
                                             &has_sps, &has_pps);
      if (!stream->sps.empty() && !stream->pps.empty() &&
          (has_sps || has_pps)) {
        stream->sequence_header_tag = stream_mux::BuildH264FlvSequenceHeaderTag(
            stream->sps, stream->pps, static_cast<uint32_t>(frame.dts_us / 1000));
        ++stream->config_generation;
      }

      const bool keyframe = stream_codec::IsKeyFrame(frame.frame_type);
      if (stream->last_pts_us > 0 && frame.pts_us > stream->last_pts_us) {
        stream->last_frame_duration_us = frame.pts_us - stream->last_pts_us;
      }
      stream->last_pts_us = frame.pts_us;

      const bool frame_has_parameter_sets =
          stream_codec::HasH264ParameterSets(units);
      if (keyframe && stream->current_segment.started &&
          frame.pts_us - stream->current_segment.start_pts_us >=
              static_cast<int64_t>(options_.hls_segment_duration_ms) * 1000) {
        FinalizeCurrentSegment(stream);
      }
      if (!stream->current_segment.started) {
        StartSegment(stream, frame.pts_us);
      }
      const std::string access_unit = stream_codec::BuildH264AnnexBAccessUnit(
          units, stream->sps, stream->pps, keyframe && !frame_has_parameter_sets);
      stream_mux::AppendH264AccessUnitToTsSegment(
          access_unit, frame.pts_us, frame.dts_us, &stream->ts_muxer_state,
          &stream->current_segment.body);
      stream->current_segment.last_pts_us = frame.pts_us;

      const std::string avcc_sample = stream_codec::BuildH264AvccSample(units);
      if (!avcc_sample.empty()) {
        const int64_t composition_time_ms =
            (frame.pts_us - frame.dts_us) / 1000;
        flv_tag = stream_mux::BuildH264FlvVideoTag(
            keyframe, static_cast<int32_t>(composition_time_ms),
            static_cast<uint32_t>(frame.dts_us / 1000), avcc_sample);
        if (keyframe) {
          stream->last_keyframe_tag = flv_tag;
        }
      }
      sequence_header_tag = stream->sequence_header_tag;
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
  struct StreamContext {
    VideoCodec codec = VideoCodec::kH264;
    StreamState state = StreamState::kClosed;
    std::string sps;
    std::string pps;
    std::string sequence_header_tag;
    std::string last_keyframe_tag;
    std::deque<StreamSegment> segments;
    HlsSegmentState current_segment;
    uint64_t next_segment_sequence = 1;
    uint64_t config_generation = 0;
    stream_mux::TsMuxerState ts_muxer_state;
    int64_t last_pts_us = -1;
    int64_t last_frame_duration_us = 33333;
  };

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

  const StreamContext *FindStream(StreamId stream_id) const {
    if (stream_id == StreamId::kMain) {
      return &main_stream_;
    }
    if (stream_id == StreamId::kSub) {
      return &sub_stream_;
    }
    return nullptr;
  }

  StreamContext *FindMutableStream(StreamId stream_id) {
    if (stream_id == StreamId::kMain) {
      return &main_stream_;
    }
    if (stream_id == StreamId::kSub) {
      return &sub_stream_;
    }
    return nullptr;
  }

  void ResetStream(StreamContext *stream, VideoCodec codec) {
    if (stream == nullptr) {
      return;
    }
    const StreamState state = stream->state;
    *stream = StreamContext{};
    stream->codec = codec;
    stream->state = state;
  }

  void StartSegment(StreamContext *stream, int64_t pts_us) {
    if (stream == nullptr) {
      return;
    }
    stream->current_segment = HlsSegmentState{};
    stream->current_segment.started = true;
    stream->current_segment.sequence = stream->next_segment_sequence++;
    stream->current_segment.start_pts_us = pts_us;
    stream->current_segment.last_pts_us = pts_us;
    stream->current_segment.body =
        stream_mux::BuildTsSegmentHeader(&stream->ts_muxer_state);
  }

  void FinalizeCurrentSegment(StreamContext *stream) {
    if (stream == nullptr || !stream->current_segment.started ||
        stream->current_segment.body.empty()) {
      return;
    }
    StreamSegment segment;
    segment.found = true;
    segment.sequence = stream->current_segment.sequence;
    segment.duration_us =
        std::max<int64_t>(stream->last_frame_duration_us,
                          stream->current_segment.last_pts_us -
                              stream->current_segment.start_pts_us +
                              stream->last_frame_duration_us);
    segment.body = stream->current_segment.body;
    stream->segments.push_back(std::move(segment));
    while (stream->segments.size() > options_.hls_playlist_depth) {
      stream->segments.pop_front();
    }
    ++stats_.hls_segments_created;
    stream->current_segment = HlsSegmentState{};
  }

  StreamHubServiceOptions options_;
  StreamHubServiceDependencies dependencies_;
  std::unique_ptr<infra::Executor> worker_executor_;
  mutable std::mutex mutex_;
  StreamFrameQueue main_pending_;
  StreamFrameQueue sub_pending_;
  bool drain_task_posted_ = false;
  StreamId last_drained_stream_ = StreamId::kSub;
  StreamContext main_stream_;
  StreamContext sub_stream_;
  std::map<StreamFlvClientId, FlvClientState> flv_clients_;
  StreamHubServiceStats stats_;
  FrameSubscriptionId main_subscription_id_ = 0;
  FrameSubscriptionId sub_subscription_id_ = 0;
  StreamFlvClientId next_flv_client_id_ = 1;
  bool started_ = false;
};

} // namespace

std::unique_ptr<IStreamHubService>
CreateStreamHubService(const StreamHubServiceOptions &options,
                       const StreamHubServiceDependencies &dependencies) {
  return std::unique_ptr<IStreamHubService>(
      new StreamHubServiceImpl(options, dependencies));
}

} // namespace live_stream
