#include "media_service.h"

#include "config_service.h"
#include "hisisdk/hisi_sdk.h"
#include "live_stream/json_utils.h"
#include "media_pipeline.h"

#include <cctype>
#include <cstdlib>
#include <limits>
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

std::string JoinField(const std::string &prefix, const std::string &name) {
  return prefix.empty() ? name : prefix + "." + name;
}

bool ParseResolution(const std::string &text, VideoSize *size) {
  if (size == nullptr) {
    return false;
  }
  const std::string::size_type split = text.find('x');
  if (split == std::string::npos || split == 0 || split + 1 >= text.size()) {
    return false;
  }
  auto parse_part = [](const std::string &part, uint32_t *value) {
    if (value == nullptr || part.empty()) {
      return false;
    }
    uint64_t parsed = 0;
    for (char ch : part) {
      if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
        return false;
      }
      parsed = parsed * 10 + static_cast<uint64_t>(ch - '0');
      if (parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
      }
    }
    if (parsed == 0) {
      return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
  };

  uint32_t width = 0;
  uint32_t height = 0;
  if (!parse_part(text.substr(0, split), &width) ||
      !parse_part(text.substr(split + 1), &height)) {
    return false;
  }
  size->width = width;
  size->height = height;
  return true;
}

bool ParseCodec(const std::string &codec, VideoCodec *parsed) {
  if (parsed == nullptr) {
    return false;
  }
  if (codec == "h264") {
    *parsed = VideoCodec::kH264;
    return true;
  }
  if (codec == "h265") {
    *parsed = VideoCodec::kH265;
    return true;
  }
  if (codec == "jpeg") {
    *parsed = VideoCodec::kJpeg;
    return true;
  }
  if (codec == "mjpeg") {
    *parsed = VideoCodec::kMjpeg;
    return true;
  }
  return false;
}

bool ParseRateControl(const std::string &rc_mode, RateControlMode *parsed) {
  if (parsed == nullptr) {
    return false;
  }
  if (rc_mode == "cbr") {
    *parsed = RateControlMode::kCbr;
    return true;
  }
  if (rc_mode == "vbr") {
    *parsed = RateControlMode::kVbr;
    return true;
  }
  if (rc_mode == "fixqp" || rc_mode == "fix_qp") {
    *parsed = RateControlMode::kFixQp;
    return true;
  }
  return false;
}

bool ParseGopMode(const std::string &gop_mode, GopMode *parsed) {
  if (parsed == nullptr) {
    return false;
  }
  if (gop_mode == "normal_p") {
    *parsed = GopMode::kNormalP;
    return true;
  }
  if (gop_mode == "dual_p") {
    *parsed = GopMode::kDualP;
    return true;
  }
  if (gop_mode == "smart_p") {
    *parsed = GopMode::kSmartP;
    return true;
  }
  return false;
}

const VideoStreamCapabilities *
FindStreamCapabilities(const MediaCapabilities &capabilities,
                       StreamId stream_id) {
  for (const VideoStreamCapabilities &stream : capabilities.streams) {
    if (stream.stream_id == stream_id) {
      return &stream;
    }
  }
  return nullptr;
}

const CodecCapability *
FindCodecCapability(const VideoStreamCapabilities &capabilities,
                    VideoCodec codec) {
  for (const CodecCapability &item : capabilities.codecs) {
    if (item.codec == codec) {
      return &item;
    }
  }
  return nullptr;
}

bool ContainsResolution(const VideoStreamCapabilities &capabilities,
                        const VideoSize &size) {
  for (const VideoResolution &item : capabilities.resolutions) {
    if (item.width == size.width && item.height == size.height) {
      return true;
    }
  }
  return false;
}

bool ContainsRateControl(const VideoStreamCapabilities &capabilities,
                         RateControlMode mode) {
  for (RateControlMode item : capabilities.rate_control_modes) {
    if (item == mode) {
      return true;
    }
  }
  return false;
}

bool ContainsString(const std::vector<std::string> &values,
                    const std::string &value) {
  for (const std::string &item : values) {
    if (item == value) {
      return true;
    }
  }
  return false;
}

ConfigResult
ValidateNumericControls(const ConfigJson &section,
                        const std::string &section_name,
                        const std::vector<NumericControlCapability> &controls) {
  for (const NumericControlCapability &control : controls) {
    int64_t value = 0;
    const std::string field = JoinField(section_name, control.name);
    if (!json_utils::Load(section, control.name.c_str(), &value, control.min,
                          control.max)) {
      return ConfigResult::Failure(field, "missing or unsupported value");
    }
  }
  return ConfigResult::Success();
}

ConfigResult
ValidateOptionControls(const ConfigJson &section,
                       const std::string &section_name,
                       const std::vector<OptionControlCapability> &controls) {
  for (const OptionControlCapability &control : controls) {
    const std::string field = JoinField(section_name, control.name);
    std::string value;
    if (json_utils::Load(section, control.name.c_str(), &value)) {
      if (!ContainsString(control.values, value)) {
        return ConfigResult::Failure(field, "unsupported value");
      }
      continue;
    }
    bool enabled = false;
    if (!json_utils::Load(section, control.name.c_str(), &enabled)) {
      return ConfigResult::Failure(field, "missing or invalid value");
    }
    if (!ContainsString(control.values, enabled ? "true" : "false")) {
      return ConfigResult::Failure(field, "unsupported value");
    }
  }
  return ConfigResult::Success();
}

ConfigResult ValidateImageConfig(const ConfigJson &value,
                                 const ImageCapabilities &capabilities) {
  if (!value.is_object()) {
    return ConfigResult::Failure("", "invalid image config");
  }
  const struct {
    const char *name;
    const std::vector<NumericControlCapability> *ranges;
    const std::vector<OptionControlCapability> *options;
  } sections[] = {
      {"basic", &capabilities.basic, nullptr},
      {"exposure", &capabilities.exposure_ranges,
       &capabilities.exposure_options},
      {"white_balance", &capabilities.white_balance_ranges,
       &capabilities.white_balance_options},
      {"enhancement", &capabilities.enhancement_ranges,
       &capabilities.enhancement_options},
      {"backlight", &capabilities.backlight_ranges,
       &capabilities.backlight_options},
      {"color_mode", nullptr, &capabilities.color_mode_options},
  };

  for (const auto &section_spec : sections) {
    const ConfigJson *section = nullptr;
    if (!json_utils::LoadObject(value, section_spec.name, &section)) {
      return ConfigResult::Failure(section_spec.name, "missing object");
    }
    if (section_spec.ranges != nullptr) {
      const ConfigResult result = ValidateNumericControls(
          *section, section_spec.name, *section_spec.ranges);
      if (!result.ok) {
        return result;
      }
    }
    if (section_spec.options != nullptr) {
      const ConfigResult result = ValidateOptionControls(
          *section, section_spec.name, *section_spec.options);
      if (!result.ok) {
        return result;
      }
    }
  }

  const ConfigJson *orientation = nullptr;
  if (!json_utils::LoadObject(value, "orientation", &orientation)) {
    return ConfigResult::Failure("orientation", "missing object");
  }
  bool mirror = false;
  if (!json_utils::Load(*orientation, "mirror", &mirror)) {
    return ConfigResult::Failure("orientation.mirror",
                                 "missing or invalid value");
  }
  if (mirror && !capabilities.mirror_supported) {
    return ConfigResult::Failure("orientation.mirror", "unsupported value");
  }
  bool flip = false;
  if (!json_utils::Load(*orientation, "flip", &flip)) {
    return ConfigResult::Failure("orientation.flip",
                                 "missing or invalid value");
  }
  if (flip && !capabilities.flip_supported) {
    return ConfigResult::Failure("orientation.flip", "unsupported value");
  }
  return ConfigResult::Success();
}

ConfigResult ParseVideoConfig(const ConfigJson &value,
                              const MediaPipelineConfig &fallback,
                              const MediaCapabilities &capabilities,
                              MediaPipelineConfig *parsed) {
  if (parsed == nullptr) {
    return ConfigResult::Failure("", "invalid video config target");
  }
  if (!value.is_object()) {
    return ConfigResult::Failure("", "invalid video config");
  }
  if (capabilities.streams.empty()) {
    return ConfigResult::Failure("", "media capabilities unavailable");
  }

  const ConfigJson *streams = nullptr;
  if (!json_utils::LoadObject(value, "streams", &streams)) {
    return ConfigResult::Failure("streams", "missing object");
  }
  const ConfigJson *source = nullptr;
  if (!json_utils::LoadObject(value, "source", &source)) {
    return ConfigResult::Failure("source", "missing object");
  }
  std::string sensor_name;
  if (!json_utils::Load(*source, "sensor", &sensor_name)) {
    return ConfigResult::Failure("source.sensor", "missing or invalid value");
  }

  MediaPipelineConfig config = fallback;
  config.main_stream.stream_id = StreamId::kMain;

  const struct {
    const char *name;
    StreamId stream_id;
  } stream_specs[] = {
      {"main", StreamId::kMain},
      {"sub", StreamId::kSub},
  };

  for (const auto &stream_spec : stream_specs) {
    const std::string stream_prefix = JoinField("streams", stream_spec.name);
    const ConfigJson *stream = nullptr;
    if (!json_utils::LoadObject(*streams, stream_spec.name, &stream)) {
      return ConfigResult::Failure(stream_prefix, "missing object");
    }

    const VideoStreamCapabilities *stream_capabilities =
        FindStreamCapabilities(capabilities, stream_spec.stream_id);
    if (stream_capabilities == nullptr) {
      return ConfigResult::Failure(stream_prefix, "missing capabilities");
    }

    bool enabled = false;
    std::string declared_name;
    std::string profile;
    std::string codec_name;
    std::string resolution;
    std::string rate_control;
    std::string gop_mode;
    bool smart_codec = false;
    int64_t fps = 0;
    int64_t bitrate = 0;
    int64_t gop = 0;
    if (!json_utils::Load(*stream, "enabled", &enabled) ||
        !json_utils::Load(*stream, "name", &declared_name) ||
        !json_utils::Load(*stream, "profile", &profile) ||
        !json_utils::Load(*stream, "codec", &codec_name) ||
        !json_utils::Load(*stream, "smart_codec", &smart_codec) ||
        !json_utils::Load(*stream, "resolution", &resolution) ||
        !json_utils::Load(*stream, "fps", &fps, 1,
                          std::numeric_limits<int64_t>::max()) ||
        !json_utils::Load(*stream, "bitrate_kbps", &bitrate, 1,
                          std::numeric_limits<int64_t>::max()) ||
        !json_utils::Load(*stream, "rate_control", &rate_control) ||
        !json_utils::Load(*stream, "gop", &gop, 1,
                          std::numeric_limits<int64_t>::max()) ||
        !json_utils::Load(*stream, "gop_mode", &gop_mode)) {
      return ConfigResult::Failure(stream_prefix, "missing required field");
    }
    (void)enabled;

    if (declared_name != stream_spec.name) {
      return ConfigResult::Failure(JoinField(stream_prefix, "name"),
                                   "unsupported value");
    }

    VideoCodec codec = VideoCodec::kH264;
    if (!ParseCodec(codec_name, &codec)) {
      return ConfigResult::Failure(JoinField(stream_prefix, "codec"),
                                   "unsupported value");
    }
    const CodecCapability *codec_capability =
        FindCodecCapability(*stream_capabilities, codec);
    if (codec_capability == nullptr) {
      return ConfigResult::Failure(JoinField(stream_prefix, "codec"),
                                   "unsupported value");
    }
    if (!codec_capability->profiles.empty() &&
        !ContainsString(codec_capability->profiles, profile)) {
      return ConfigResult::Failure(JoinField(stream_prefix, "profile"),
                                   "unsupported value");
    }

    VideoSize parsed_size;
    if (!ParseResolution(resolution, &parsed_size) ||
        !ContainsResolution(*stream_capabilities, parsed_size)) {
      return ConfigResult::Failure(JoinField(stream_prefix, "resolution"),
                                   "unsupported value");
    }
    if (fps < static_cast<int64_t>(stream_capabilities->frame_rate.min_fps) ||
        fps > static_cast<int64_t>(stream_capabilities->frame_rate.max_fps)) {
      return ConfigResult::Failure(JoinField(stream_prefix, "fps"),
                                   "unsupported value");
    }
    if (bitrate < static_cast<int64_t>(stream_capabilities->bitrate.min_kbps) ||
        bitrate > static_cast<int64_t>(stream_capabilities->bitrate.max_kbps)) {
      return ConfigResult::Failure(JoinField(stream_prefix, "bitrate_kbps"),
                                   "unsupported value");
    }

    RateControlMode parsed_rate_control = RateControlMode::kCbr;
    if (!ParseRateControl(rate_control, &parsed_rate_control) ||
        !ContainsRateControl(*stream_capabilities, parsed_rate_control)) {
      return ConfigResult::Failure(JoinField(stream_prefix, "rate_control"),
                                   "unsupported value");
    }
    if (gop < static_cast<int64_t>(stream_capabilities->gop.min) ||
        gop > static_cast<int64_t>(stream_capabilities->gop.max)) {
      return ConfigResult::Failure(JoinField(stream_prefix, "gop"),
                                   "unsupported value");
    }

    GopMode parsed_gop_mode = GopMode::kNormalP;
    if (!ParseGopMode(gop_mode, &parsed_gop_mode)) {
      return ConfigResult::Failure(JoinField(stream_prefix, "gop_mode"),
                                   "unsupported value");
    }
    if (smart_codec && !stream_capabilities->smart_codec_supported) {
      return ConfigResult::Failure(JoinField(stream_prefix, "smart_codec"),
                                   "unsupported value");
    }

    if (stream_spec.stream_id == StreamId::kMain) {
      config.main_stream.codec = codec;
      config.main_stream.size = parsed_size;
      config.main_stream.frame_rate.source_fps = static_cast<int32_t>(fps);
      config.main_stream.frame_rate.target_fps = static_cast<int32_t>(fps);
      config.main_stream.bitrate_kbps = static_cast<uint32_t>(bitrate);
      config.main_stream.gop = static_cast<uint32_t>(gop);
      config.main_stream.rc_mode = parsed_rate_control;
      config.main_stream.gop_mode = parsed_gop_mode;
    }
  }

  if (!IsValidMediaPipelineConfig(config)) {
    return ConfigResult::Failure("streams.main",
                                 "invalid media pipeline config");
  }
  *parsed = config;
  return ConfigResult::Success();
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
        item.second.second->OnSourceStateChanged(item.second.first.stream_id,
                                                 stream_state);
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
    if (config.main_stream.size.width ==
            pipeline.config().main_stream.size.width &&
        config.main_stream.size.height ==
            pipeline.config().main_stream.size.height &&
        config.main_stream.codec == pipeline.config().main_stream.codec &&
        config.main_stream.frame_rate.target_fps ==
            pipeline.config().main_stream.frame_rate.target_fps &&
        config.main_stream.bitrate_kbps ==
            pipeline.config().main_stream.bitrate_kbps &&
        config.main_stream.gop == pipeline.config().main_stream.gop &&
        config.main_stream.rc_mode == pipeline.config().main_stream.rc_mode &&
        config.main_stream.gop_mode == pipeline.config().main_stream.gop_mode) {
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
  return stream_id == impl_->pipeline.config().main_stream.stream_id;
}

bool MediaService::IsStreamStarted(StreamId stream_id) const {
  if (impl_ == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->state == ServiceState::kStarted &&
         stream_id == impl_->pipeline.config().main_stream.stream_id;
}

VideoCodec MediaService::GetStreamCodec(StreamId stream_id) const {
  if (impl_ == nullptr) {
    return VideoCodec::kH264;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (stream_id == impl_->pipeline.config().main_stream.stream_id) {
    return impl_->pipeline.config().main_stream.codec;
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
  if (sink == nullptr || !IsValidMediaStream(options.stream_id)) {
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
  if (!IsValidMediaStream(stream_id)) {
    return false;
  }
  if (impl_->state != ServiceState::kStarted) {
    return false;
  }
  hisisdk::IHisiSdk *sdk = impl_->options.sdk != nullptr
                               ? impl_->options.sdk
                               : &hisisdk::DefaultSdk();
  return sdk->RequestIdr(impl_->pipeline.config().venc_channel);
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
