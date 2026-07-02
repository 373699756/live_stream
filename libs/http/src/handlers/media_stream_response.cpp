#include "handlers/media_stream_response.h"

#include "http_stream_id_json.h"

#include "config.h"
#include "json_reader.h"
#include "media/media_streams.h"
#include "webrtc.h"

#include <cstdint>
#include <string>
#include <vector>

namespace live_stream {
namespace {

const char *CodecToJsonString(Codec codec) {
    switch (codec) {
        case Codec::kH264:
            return "h264";
        case Codec::kH265:
            return "h265";
        case Codec::kJpeg:
            return "jpeg";
        case Codec::kMjpeg:
            return "mjpeg";
    }
    return "unknown";
}

const char *RateControlToJsonString(RateControlMode mode) {
    switch (mode) {
        case RateControlMode::kCbr:
            return "cbr";
        case RateControlMode::kVbr:
            return "vbr";
        case RateControlMode::kFixQp:
            return "fixqp";
    }
    return "cbr";
}

Json NumericControlToJson(const NumericControlCapability &capability) {
    Json root = Json::object();
    root["min"] = capability.min;
    root["max"] = capability.max;
    root["default"] = capability.default_value;
    root["live_update_supported"] = capability.live_update_supported;
    return root;
}

Json OptionControlToJson(const OptionControlCapability &capability) {
    Json root = Json::object();
    Json values = Json::array();
    for (const std::string &value : capability.values) {
        values.push_back(value);
    }
    root["values"] = values;
    root["default"] = capability.default_value;
    root["live_update_supported"] = capability.live_update_supported;
    return root;
}

Json NumericControlsToJson(
    const std::vector<NumericControlCapability> &capabilities) {
    Json root = Json::object();
    for (const NumericControlCapability &capability : capabilities) {
        root[capability.name] = NumericControlToJson(capability);
    }
    return root;
}

Json OptionControlsToJson(
    const std::vector<OptionControlCapability> &capabilities) {
    Json root = Json::object();
    for (const OptionControlCapability &capability : capabilities) {
        root[capability.name] = OptionControlToJson(capability);
    }
    return root;
}

Json VideoStreamCapabilitiesToJson(
    StreamId stream_id, const VideoStreamCapabilities *capabilities) {
    Json root = Json::object();
    root["stream"] = StreamIdToJsonString(stream_id);
    root["available"] = capabilities != nullptr;
    root["codecs"] = Json::array();
    root["resolutions"] = Json::array();
    root["rate_control"] = Json::array();
    if (capabilities == nullptr) {
        return root;
    }

    Json codecs = Json::array();
    for (const CodecCapability &codec : capabilities->codecs) {
        Json item = Json::object();
        item["codec"] = CodecToJsonString(codec.codec);
        Json profiles = Json::array();
        for (const std::string &profile : codec.profiles) {
            profiles.push_back(profile);
        }
        item["profiles"] = profiles;
        codecs.push_back(item);
    }
    root["codecs"] = codecs;

    Json resolutions = Json::array();
    for (const VideoResolution &resolution : capabilities->resolutions) {
        Json item = Json::object();
        item["width"] = resolution.width;
        item["height"] = resolution.height;
        resolutions.push_back(item);
    }
    root["resolutions"] = resolutions;

    Json fps = Json::object();
    fps["min"] = capabilities->frame_rate.min_fps;
    fps["max"] = capabilities->frame_rate.max_fps;
    root["fps"] = fps;

    Json bitrate = Json::object();
    bitrate["min"] = capabilities->bitrate.min_kbps;
    bitrate["max"] = capabilities->bitrate.max_kbps;
    root["bitrate_kbps"] = bitrate;

    Json rate_control = Json::array();
    for (RateControlMode mode : capabilities->rate_control_modes) {
        rate_control.push_back(RateControlToJsonString(mode));
    }
    root["rate_control"] = rate_control;

    Json gop = Json::object();
    gop["min"] = capabilities->gop.min;
    gop["max"] = capabilities->gop.max;
    root["gop"] = gop;
    root["smart_codec"] = capabilities->smart_codec_supported;
    root["roi_supported"] = capabilities->roi_supported;
    root["max_roi_regions"] = capabilities->max_roi_regions;
    return root;
}

const VideoStreamCapabilities *FindStreamCapabilities(
    const MediaCapabilities &capabilities, StreamId stream_id) {
    for (const VideoStreamCapabilities &stream : capabilities.streams) {
        if (stream.stream_id == stream_id) {
            return &stream;
        }
    }
    return nullptr;
}

Json ImageCapabilitiesToJson(const ImageCapabilities &capabilities) {
    Json root = Json::object();
    root["basic"] = NumericControlsToJson(capabilities.basic);

    Json exposure = Json::object();
    exposure["options"] =
        OptionControlsToJson(capabilities.exposure_options);
    exposure["ranges"] = NumericControlsToJson(capabilities.exposure_ranges);
    root["exposure"] = exposure;

    Json white_balance = Json::object();
    white_balance["options"] =
        OptionControlsToJson(capabilities.white_balance_options);
    white_balance["ranges"] =
        NumericControlsToJson(capabilities.white_balance_ranges);
    root["white_balance"] = white_balance;

    Json enhancement = Json::object();
    enhancement["options"] =
        OptionControlsToJson(capabilities.enhancement_options);
    enhancement["ranges"] =
        NumericControlsToJson(capabilities.enhancement_ranges);
    root["enhancement"] = enhancement;

    Json backlight = Json::object();
    backlight["options"] =
        OptionControlsToJson(capabilities.backlight_options);
    backlight["ranges"] = NumericControlsToJson(capabilities.backlight_ranges);
    root["backlight"] = backlight;

    root["color_mode"] =
        OptionControlsToJson(capabilities.color_mode_options);

    Json lens_correction = Json::object();
    lens_correction["supported"] = capabilities.lens_correction_supported;
    lens_correction["min_width"] = capabilities.lens_correction_min_width;
    lens_correction["min_height"] = capabilities.lens_correction_min_height;
    lens_correction["options"] =
        OptionControlsToJson(capabilities.lens_correction_options);
    lens_correction["ranges"] =
        NumericControlsToJson(capabilities.lens_correction_ranges);
    root["lens_correction"] = lens_correction;

    Json stabilization = Json::object();
    stabilization["supported"] = capabilities.stabilization_supported;
    stabilization["min_width"] = capabilities.stabilization_min_width;
    stabilization["min_height"] = capabilities.stabilization_min_height;
    stabilization["options"] =
        OptionControlsToJson(capabilities.stabilization_options);
    stabilization["ranges"] =
        NumericControlsToJson(capabilities.stabilization_ranges);
    root["stabilization"] = stabilization;

    Json orientation = Json::object();
    orientation["mirror"] = capabilities.mirror_supported;
    orientation["flip"] = capabilities.flip_supported;
    root["orientation"] = orientation;
    return root;
}

bool IsWebrtcReady(const WebrtcStats &stats) {
    return stats.enabled && stats.signaling_ready && stats.ice_ready &&
           stats.dtls_ready && stats.srtp_ready;
}

bool IsWebrtcSupported(Codec codec, const WebrtcStats &stats) {
    return stats.enabled && (codec == Codec::kH264 ||
                             codec == Codec::kH265);
}

struct VideoStreamDisplayConfig {
    std::string resolution;
    uint32_t fps = 0;
    uint32_t bitrate_kbps = 0;
    bool has_resolution = false;
    bool has_fps = false;
    bool has_bitrate_kbps = false;
};

bool ReadVideoStreamDisplayConfig(IConfig *config,
                                  StreamId stream_id,
                                  VideoStreamDisplayConfig *display_config) {
    if (config == nullptr || display_config == nullptr) {
        return false;
    }
    const Json video = config->Get("video");
    if (!video.is_object()) {
        return false;
    }
    const auto streams = video.find("streams");
    if (streams == video.end() || !streams->is_object()) {
        return false;
    }
    const auto stream = streams->find(StreamIdToJsonString(stream_id));
    if (stream == streams->end() || !stream->is_object()) {
        return false;
    }

    std::string resolution;
    if (json_reader::ReadField(*stream, "resolution", &resolution) &&
        !resolution.empty()) {
        display_config->resolution = resolution;
        display_config->has_resolution = true;
    }
    uint32_t fps = 0;
    if (json_reader::ReadField(*stream, "fps", &fps) && fps > 0) {
        display_config->fps = fps;
        display_config->has_fps = true;
    }
    uint32_t bitrate_kbps = 0;
    if (json_reader::ReadField(*stream, "bitrate_kbps", &bitrate_kbps) &&
        bitrate_kbps > 0) {
        display_config->bitrate_kbps = bitrate_kbps;
        display_config->has_bitrate_kbps = true;
    }
    return display_config->has_resolution || display_config->has_fps ||
           display_config->has_bitrate_kbps;
}

void AddVideoStreamDisplayConfig(Json *root,
                                 const VideoStreamDisplayConfig &config) {
    if (root == nullptr) {
        return;
    }
    if (config.has_resolution) {
        (*root)["resolution"] = config.resolution;
    }
    if (config.has_fps) {
        (*root)["fps"] = config.fps;
    }
    if (config.has_bitrate_kbps) {
        (*root)["bitrate_kbps"] = config.bitrate_kbps;
    }
}

}  // namespace

Json BuildMediaCapabilitiesResponse(const MediaCapabilities &capabilities) {
    Json root = Json::object();
    Json streams = Json::object();
    streams["main"] = VideoStreamCapabilitiesToJson(
        StreamId::kMain,
        FindStreamCapabilities(capabilities, StreamId::kMain));
    streams["sub"] = VideoStreamCapabilitiesToJson(
        StreamId::kSub,
        FindStreamCapabilities(capabilities, StreamId::kSub));
    root["streams"] = streams;
    root["image"] = ImageCapabilitiesToJson(capabilities.image);
    return root;
}

Json BuildMediaStreamResponse(StreamId stream_id,
                              IConfig *config,
                              MediaStreams *media_streams,
                              const WebrtcStats &webrtc_stats) {
    MediaStreamInfo stream_info;
    MediaStreamStats stats;
    bool media_stream_available = false;
    if (media_streams != nullptr) {
        stream_info = media_streams->GetStreamInfo(stream_id);
        stats = media_streams->GetStreamStats();
        media_stream_available = media_streams->IsStreamAvailable(stream_id);
    }

    Json root = Json::object();
    root["stream"] = StreamIdToJsonString(stream_id);
    root["available"] = media_stream_available;
    root["running"] = stream_info.running;
    root["codec"] = CodecToJsonString(stream_info.codec);
    root["codec_generation"] = stream_info.codec_generation;
    root["track_ready"] = stream_info.track_ready;
    root["hls_supported"] = stream_info.hls_supported;
    root["hls_ready"] = stream_info.hls_ready;
    root["http_flv_supported"] = stream_info.flv_supported;
    root["http_flv_ready"] = stream_info.flv_ready;
    root["mjpeg_supported"] = stream_info.mjpeg_supported;
    root["mjpeg_ready"] = stream_info.mjpeg_ready;
    const bool webrtc_supported =
        IsWebrtcSupported(stream_info.codec, webrtc_stats);
    root["webrtc_supported"] = webrtc_supported;
    root["webrtc_ready"] =
        stream_info.running && stream_info.track_ready && webrtc_supported &&
        IsWebrtcReady(webrtc_stats);
    root["active_subscriptions"] = stats.active_subscriptions;
    root["preview_clients"] =
        stats.active_flv_clients + stats.active_mjpeg_clients;
    root["cached_frames"] = stats.cached_frames;
    root["cached_bytes"] = stats.cached_bytes;
    root["hls_bytes"] = stream_info.hls_current_segment_size;
    root["last_dts"] = stream_info.last_dts_us;
    root["last_reset_reason"] = stream_info.last_reset_reason;
    VideoStreamDisplayConfig display_config;
    if (ReadVideoStreamDisplayConfig(config, stream_id, &display_config)) {
        AddVideoStreamDisplayConfig(&root, display_config);
    }
    return root;
}

}  // namespace live_stream
