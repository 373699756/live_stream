#include "media_config_codec.h"

#include "live_stream/json_utils.h"
#include "media_pipeline.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace live_stream {
namespace media_internal {
namespace {

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

}  // namespace

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
  config.sub_stream.stream_id = StreamId::kSub;

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

    VideoStreamConfig *target_stream =
        stream_spec.stream_id == StreamId::kSub ? &config.sub_stream
                                                : &config.main_stream;
    target_stream->stream_id = stream_spec.stream_id;
    target_stream->enabled = enabled;
    target_stream->codec = codec;
    target_stream->size = parsed_size;
    target_stream->frame_rate.source_fps = static_cast<int32_t>(fps);
    target_stream->frame_rate.target_fps = static_cast<int32_t>(fps);
    target_stream->bitrate_kbps = static_cast<uint32_t>(bitrate);
    target_stream->gop = static_cast<uint32_t>(gop);
    target_stream->rc_mode = parsed_rate_control;
    target_stream->gop_mode = parsed_gop_mode;
  }

  if (!IsValidMediaPipelineConfig(config)) {
    return ConfigResult::Failure("streams.main",
                                 "invalid media pipeline config");
  }
  *parsed = config;
  return ConfigResult::Success();
}

}  // namespace media_internal
}  // namespace live_stream
