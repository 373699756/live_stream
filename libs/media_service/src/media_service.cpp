#include "media_service.h"

#include "config_service.h"
#include "hisisdk/hisi_sdk.h"
#include "media_config_codec.h"
#include "media_pipeline.h"

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

enum class ServiceState {
  kCreated = 0,
  kInitialized,
  kStarted,
  kStopping,
  kStopped,
  kDeinitialized,
};

using media_internal::ParseVideoConfig;
using media_internal::ValidateImageConfig;

const VideoStreamConfig *FindConfiguredStream(
    const MediaPipelineConfig &config,
    StreamId stream_id) {
  if (stream_id == config.main_stream.stream_id) {
    return &config.main_stream;
  }
  if (stream_id == config.sub_stream.stream_id) {
    return &config.sub_stream;
  }
  return nullptr;
}

int32_t VencChannelForStream(const MediaPipelineConfig &config,
                             StreamId stream_id) {
  if (stream_id == config.sub_stream.stream_id) {
    return config.sub_venc_channel;
  }
  if (stream_id == config.main_stream.stream_id) {
    return config.venc_channel;
  }
  return -1;
}

} // namespace

struct MediaService::Impl {
  explicit Impl(const MediaServiceOptions &service_options)
      : options(service_options),
        pipeline(service_options.default_config, service_options.sdk) {
    pipeline.SetFrameCallback(&Impl::OnPipelineFrame, this);
  }

  MediaServiceOptions options;
  MediaPipeline pipeline;
  ServiceState state = ServiceState::kCreated;
  EncodedFrameCallback callback = nullptr;
  void *callback_user = nullptr;
  std::map<FrameSubscriptionId, std::pair<FrameSubscribeOptions, IFrameSink *>>
      sinks;
  FrameSubscriptionId next_subscription_id = 1;
  MediaServiceStats stats;
  ConfigJson image_config = ConfigJson::object();
  mutable std::mutex mutex;
  bool video_config_attached = false;
  bool image_config_attached = false;

  bool Prepare() {
    std::lock_guard<std::mutex> lock(mutex);
    if (state == ServiceState::kInitialized ||
        state == ServiceState::kStarted || state == ServiceState::kStopped) {
      return true;
    }
    if (state != ServiceState::kCreated &&
        state != ServiceState::kDeinitialized) {
      return false;
    }

    if (!pipeline.InitSystem()) {
      pipeline.DeinitSystem();
      return false;
    }

    if (options.config_service != nullptr) {
      ConfigJson video_config = options.config_service->GetValue("video");
      if (video_config.is_object()) {
        const ConfigResult result = ApplyVideoConfig(video_config);
        if (!result.ok) {
          pipeline.DeinitSystem();
          return false;
        }
      }
      ConfigJson next_image_config = options.config_service->GetValue("image");
      if (next_image_config.is_object()) {
        const ConfigResult result = ApplyImageConfig(next_image_config);
        if (!result.ok) {
          pipeline.DeinitSystem();
          return false;
        }
      }
      if (!video_config_attached) {
        ConfigAttachment attachment;
        attachment.validate = [this](const ConfigJson &value) {
          std::lock_guard<std::mutex> guard(mutex);
          return CheckVideoConfig(value);
        };
        attachment.apply = [this](const ConfigJson &value) {
          std::lock_guard<std::mutex> guard(mutex);
          return ApplyVideoConfig(value);
        };
        if (!options.config_service->AttachConfig("video", attachment)) {
          pipeline.DeinitSystem();
          return false;
        }
        video_config_attached = true;
      }
      if (!image_config_attached) {
        ConfigAttachment attachment;
        attachment.validate = [this](const ConfigJson &value) {
          std::lock_guard<std::mutex> guard(mutex);
          return CheckImageConfig(value);
        };
        attachment.apply = [this](const ConfigJson &value) {
          std::lock_guard<std::mutex> guard(mutex);
          return ApplyImageConfig(value);
        };
        if (!options.config_service->AttachConfig("image", attachment)) {
          if (video_config_attached) {
            (void)options.config_service->DetachConfig("video");
            video_config_attached = false;
          }
          pipeline.DeinitSystem();
          return false;
        }
        image_config_attached = true;
      }
    }

    state = ServiceState::kInitialized;
    return true;
  }

  void Release() {
    bool detach_video = false;
    bool detach_image = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (state == ServiceState::kStarted) {
        state = ServiceState::kStopping;
        NotifySourceState(StreamState::kClosed);
        pipeline.Stop();
        state = ServiceState::kStopped;
      }
      if (state != ServiceState::kDeinitialized &&
          state != ServiceState::kCreated) {
        pipeline.DeinitSystem();
        state = ServiceState::kDeinitialized;
      }
      detach_video = video_config_attached;
      detach_image = image_config_attached;
      video_config_attached = false;
      image_config_attached = false;
    }

    if (options.config_service != nullptr) {
      if (detach_video) {
        (void)options.config_service->DetachConfig("video");
      }
      if (detach_image) {
        (void)options.config_service->DetachConfig("image");
      }
    }
  }

  static void OnPipelineFrame(const EncodedFrame &frame, void *user) {
    if (user != nullptr) {
      static_cast<Impl *>(user)->DispatchFrame(frame);
    }
  }

  void DispatchFrame(const EncodedFrame &frame) {
    EncodedFrameCallback frame_callback = nullptr;
    void *frame_callback_user = nullptr;
    std::vector<IFrameSink *> matching_sinks;
    {
      std::lock_guard<std::mutex> guard(mutex);
      frame_callback = callback;
      frame_callback_user = callback_user;
      for (const auto &item : sinks) {
        if (item.second.first.stream_id == frame.stream_id &&
            item.second.second != nullptr) {
          matching_sinks.push_back(item.second.second);
        }
      }
    }
    if (frame_callback != nullptr) {
      frame_callback(frame, frame_callback_user);
    }
    for (IFrameSink *sink : matching_sinks) {
      sink->OnFrame(frame);
    }
  }

  void NotifySourceState(StreamState stream_state) {
    for (const auto &item : sinks) {
      if (item.second.second != nullptr) {
        const VideoStreamConfig *stream =
            FindConfiguredStream(pipeline.config(), item.second.first.stream_id);
        const bool stream_running =
            stream_state == StreamState::kRunning && stream != nullptr &&
            stream->enabled;
        const StreamState effective_state =
            stream_running ? stream_state : StreamState::kClosed;
        item.second.second->OnSourceStateChanged(item.second.first.stream_id,
                                                 effective_state);
      }
    }
  }

  ConfigResult CheckVideoConfig(const ConfigJson &value) const {
    MediaPipelineConfig parsed;
    return ParseVideoConfig(value, pipeline.config(),
                            pipeline.GetCapabilities(), &parsed);
  }

  ConfigResult ApplyVideoConfig(const ConfigJson &value) {
    MediaPipelineConfig parsed;
    const ConfigResult result = ParseVideoConfig(
        value, pipeline.config(), pipeline.GetCapabilities(), &parsed);
    if (!result.ok) {
      ++stats.config_apply_failed_count;
      return result;
    }
    if (!ApplyPipelineConfig(parsed)) {
      return ConfigResult::Failure("streams.main", "apply failed");
    }
    return ConfigResult::Success();
  }

  ConfigResult CheckImageConfig(const ConfigJson &value) const {
    return ValidateImageConfig(value, pipeline.GetCapabilities().image);
  }

  ConfigResult ApplyImageConfig(const ConfigJson &value) {
    const ConfigResult result =
        ValidateImageConfig(value, pipeline.GetCapabilities().image);
    if (!result.ok) {
      return result;
    }
    image_config = value;
    return ConfigResult::Success();
  }

  bool ApplyPipelineConfig(const MediaPipelineConfig &config) {
    if (!IsValidMediaPipelineConfig(config)) {
      ++stats.config_apply_failed_count;
      return false;
    }
    if (VideoStreamConfigEqual(config.main_stream,
                               pipeline.config().main_stream) &&
        VideoStreamConfigEqual(config.sub_stream, pipeline.config().sub_stream)) {
      ++stats.config_apply_count;
      return true;
    }

    const bool was_started = state == ServiceState::kStarted;
    const bool was_initialized = pipeline.system_initialized();
    const MediaPipelineConfig previous = pipeline.config();
    if (was_started) {
      NotifySourceState(StreamState::kClosed);
      pipeline.Stop();
    }
    if (was_initialized) {
      pipeline.DeinitSystem();
    }

    pipeline.SetConfig(config);
    bool ok = !was_initialized || pipeline.InitSystem();
    if (ok && was_started) {
      ok = pipeline.Start();
    }
    if (ok) {
      ++stats.config_apply_count;
      if (was_started) {
        ++stats.restart_count;
        NotifySourceState(StreamState::kRunning);
      }
      return true;
    }

    pipeline.Stop();
    if (pipeline.system_initialized()) {
      pipeline.DeinitSystem();
    }
    pipeline.SetConfig(previous);
    if (was_initialized && pipeline.InitSystem() && was_started &&
        pipeline.Start()) {
      NotifySourceState(StreamState::kRunning);
    } else if (was_started) {
      state = ServiceState::kStopped;
      NotifySourceState(StreamState::kError);
    }
    ++stats.config_apply_failed_count;
    return false;
  }

  static bool VideoStreamConfigEqual(const VideoStreamConfig &lhs,
                                     const VideoStreamConfig &rhs) {
    return lhs.stream_id == rhs.stream_id &&
           lhs.enabled == rhs.enabled &&
           lhs.size.width == rhs.size.width &&
           lhs.size.height == rhs.size.height &&
           lhs.codec == rhs.codec &&
           lhs.frame_rate.target_fps == rhs.frame_rate.target_fps &&
           lhs.bitrate_kbps == rhs.bitrate_kbps &&
           lhs.gop == rhs.gop &&
           lhs.rc_mode == rhs.rc_mode &&
           lhs.gop_mode == rhs.gop_mode;
  }
};

MediaService::MediaService() : MediaService(MediaServiceOptions{}) {}

MediaService::MediaService(const MediaPipelineConfig &config)
    : MediaService(MediaServiceOptions{config, nullptr, nullptr}) {}

MediaService::MediaService(const MediaServiceOptions &options)
    : impl_(new Impl(options)) {}

MediaService::~MediaService() {
  if (impl_ != nullptr) {
    impl_->Release();
    delete impl_;
    impl_ = nullptr;
  }
}

bool MediaService::Start() {
  if (impl_ == nullptr) {
    return false;
  }
  bool need_init = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    need_init = impl_->state == ServiceState::kCreated ||
                impl_->state == ServiceState::kDeinitialized;
  }
  if (need_init && !impl_->Prepare()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->state == ServiceState::kStarted) {
    return true;
  }
  if (impl_->state == ServiceState::kStopped) {
    impl_->state = ServiceState::kInitialized;
  }
  if (impl_->state != ServiceState::kInitialized) {
    return false;
  }

  if (!impl_->pipeline.Start()) {
    impl_->pipeline.Stop();
    impl_->state = ServiceState::kInitialized;
    return false;
  }

  impl_->state = ServiceState::kStarted;
  impl_->NotifySourceState(StreamState::kRunning);
  return true;
}

void MediaService::Stop() {
  if (impl_ == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->state != ServiceState::kStarted) {
    if (impl_->state == ServiceState::kStopping) {
      impl_->state = ServiceState::kStopped;
    }
    return;
  }

  impl_->state = ServiceState::kStopping;
  impl_->NotifySourceState(StreamState::kClosed);
  impl_->pipeline.Stop();
  impl_->state = ServiceState::kStopped;
}

bool MediaService::IsStarted() const {
  if (impl_ == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->state == ServiceState::kStarted;
}

bool MediaService::IsStreamSupported(StreamId stream_id) const {
  if (impl_ == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return FindConfiguredStream(impl_->pipeline.config(), stream_id) != nullptr;
}

bool MediaService::IsStreamStarted(StreamId stream_id) const {
  if (impl_ == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const VideoStreamConfig *stream =
      FindConfiguredStream(impl_->pipeline.config(), stream_id);
  return impl_->state == ServiceState::kStarted && stream != nullptr &&
         stream->enabled;
}

VideoCodec MediaService::GetStreamCodec(StreamId stream_id) const {
  if (impl_ == nullptr) {
    return VideoCodec::kH264;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const VideoStreamConfig *stream =
      FindConfiguredStream(impl_->pipeline.config(), stream_id);
  if (stream != nullptr) {
    return stream->codec;
  }
  return VideoCodec::kH264;
}

const char *MediaService::StaticName() { return "media_service"; }

FrameSubscriptionId
MediaService::SubscribeFrames(const FrameSubscribeOptions &options,
                              IFrameSink *sink) {
  if (impl_ == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const VideoStreamConfig *stream =
      FindConfiguredStream(impl_->pipeline.config(), options.stream_id);
  if (sink == nullptr || stream == nullptr || !stream->enabled) {
    return 0;
  }
  if (impl_->state != ServiceState::kStarted) {
    return 0;
  }

  const FrameSubscriptionId id = impl_->next_subscription_id++;
  impl_->sinks[id] = std::make_pair(options, sink);
  impl_->stats.subscription_count = static_cast<uint32_t>(impl_->sinks.size());
  sink->OnSourceStateChanged(options.stream_id, StreamState::kRunning);
  return id;
}

bool MediaService::UnsubscribeFrames(FrameSubscriptionId subscription_id) {
  if (impl_ == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  auto it = impl_->sinks.find(subscription_id);
  if (it == impl_->sinks.end()) {
    return false;
  }
  impl_->sinks.erase(it);
  impl_->stats.subscription_count = static_cast<uint32_t>(impl_->sinks.size());
  return true;
}

bool MediaService::RequestKeyFrame(StreamId stream_id, KeyFrameReason reason) {
  if (impl_ == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  (void)reason;
  const VideoStreamConfig *stream =
      FindConfiguredStream(impl_->pipeline.config(), stream_id);
  if (impl_->state != ServiceState::kStarted || stream == nullptr ||
      !stream->enabled) {
    return false;
  }
  hisisdk::IHisiSdk *sdk = impl_->options.sdk != nullptr
                               ? impl_->options.sdk
                               : &hisisdk::DefaultSdk();
  return sdk->RequestIdr(VencChannelForStream(impl_->pipeline.config(),
                                              stream_id));
}

MediaCapabilities MediaService::GetCapabilities() const {
  if (impl_ == nullptr) {
    return MediaCapabilities{};
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->pipeline.GetCapabilities();
}

bool MediaService::SetEncodedFrameCallback(EncodedFrameCallback callback,
                                           void *user) {
  if (impl_ == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->state == ServiceState::kStarted) {
    return false;
  }
  impl_->callback = callback;
  impl_->callback_user = user;
  return true;
}

MediaChannels MediaService::GetChannels() const {
  if (impl_ == nullptr) {
    return MediaChannels{};
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->pipeline.system_initialized()) {
    return MediaChannels{};
  }
  return impl_->pipeline.channels();
}

MediaServiceStats MediaService::GetStats() const {
  if (impl_ == nullptr) {
    return MediaServiceStats{};
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  MediaServiceStats stats = impl_->stats;
  stats.subscription_count = static_cast<uint32_t>(impl_->sinks.size());
  return stats;
}

MppChannel MediaService::GetMainVpssChannel() const {
  return GetChannels().vpss;
}

MppChannel MediaService::GetMainVencChannel() const {
  return GetChannels().venc;
}

} // namespace live_stream
