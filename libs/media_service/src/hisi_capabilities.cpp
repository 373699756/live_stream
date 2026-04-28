#include "hisi_sdk_default.h"

#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace hisisdk {
namespace {

CodecCapability H264Capability() {
    CodecCapability capability;
    capability.codec = infra::VideoCodec::kH264;
    capability.profiles.push_back("baseline");
    capability.profiles.push_back("main");
    capability.profiles.push_back("high");
    return capability;
}

CodecCapability H265Capability() {
    CodecCapability capability;
    capability.codec = infra::VideoCodec::kH265;
    capability.profiles.push_back("main");
    return capability;
}

CodecCapability JpegCapability() {
    CodecCapability capability;
    capability.codec = infra::VideoCodec::kJpeg;
    capability.profiles.push_back("baseline");
    return capability;
}

CodecCapability MjpegCapability() {
    CodecCapability capability;
    capability.codec = infra::VideoCodec::kMjpeg;
    capability.profiles.push_back("baseline");
    return capability;
}

NumericControlCapability Range(const char* name,
                               int32_t min,
                               int32_t max,
                               int32_t default_value) {
    NumericControlCapability capability;
    capability.name = name;
    capability.min = min;
    capability.max = max;
    capability.default_value = default_value;
    return capability;
}

OptionControlCapability Options(const char* name,
                                std::vector<std::string> values,
                                const char* default_value) {
    OptionControlCapability capability;
    capability.name = name;
    capability.values = std::move(values);
    capability.default_value = default_value;
    return capability;
}

ImageCapabilities DefaultImageCapabilities() {
    ImageCapabilities image;
    image.basic.push_back(Range("brightness", 0, 100, 50));
    image.basic.push_back(Range("contrast", 0, 100, 50));
    image.basic.push_back(Range("saturation", 0, 100, 50));
    image.basic.push_back(Range("sharpness", 0, 100, 50));
    image.basic.push_back(Range("hue", 0, 100, 50));

    image.exposure_options.push_back(
        Options("mode", {"auto", "manual"}, "auto"));
    image.exposure_options.push_back(
        Options("anti_flicker", {"50hz", "60hz", "off"}, "50hz"));
    image.exposure_options.push_back(
        Options("exposure_time", {"auto", "1/25", "1/50", "1/100",
                                  "1/250"},
                "auto"));
    image.exposure_options.push_back(
        Options("gain", {"auto", "low", "medium", "high"}, "auto"));
    image.exposure_options.push_back(
        Options("slow_shutter", {"false", "true"}, "true"));
    image.exposure_options.push_back(
        Options("max_exposure_time", {"1/12", "1/25", "1/50"}, "1/25"));
    image.exposure_ranges.push_back(Range("compensation", 0, 100, 50));

    image.white_balance_options.push_back(
        Options("mode", {"auto", "manual", "indoor", "outdoor"}, "auto"));
    image.white_balance_ranges.push_back(Range("red_gain", 0, 100, 50));
    image.white_balance_ranges.push_back(Range("blue_gain", 0, 100, 50));

    image.enhancement_ranges.push_back(Range("denoise_2d", 0, 100, 50));
    image.enhancement_ranges.push_back(Range("denoise_3d", 0, 100, 50));
    image.enhancement_ranges.push_back(Range("gamma", 0, 100, 50));
    image.enhancement_options.push_back(
        Options("defog", {"false", "true"}, "false"));

    image.backlight_options.push_back(
        Options("mode", {"off", "wdr", "blc", "hlc"}, "off"));
    image.backlight_ranges.push_back(Range("level", 0, 100, 50));
    image.color_mode_options.push_back(
        Options("mode", {"color", "black_white", "auto"}, "color"));
    image.mirror_supported = true;
    image.flip_supported = true;
    return image;
}

MediaCapabilities DefaultCapabilities() {
    MediaCapabilities capabilities;

    VideoStreamCapabilities main_stream;
    main_stream.stream_id = infra::StreamId::kMain;
    main_stream.codecs.push_back(H264Capability());
    main_stream.codecs.push_back(H265Capability());
    main_stream.codecs.push_back(JpegCapability());
    main_stream.codecs.push_back(MjpegCapability());
    main_stream.resolutions.push_back(VideoResolution{3840, 2160});
    main_stream.resolutions.push_back(VideoResolution{2560, 1440});
    main_stream.resolutions.push_back(VideoResolution{1920, 1080});
    main_stream.resolutions.push_back(VideoResolution{1280, 720});
    main_stream.frame_rate = FrameRateRange{1, 30};
    main_stream.bitrate = BitrateRange{512, 8192};
    main_stream.rate_control_modes.push_back(RateControlMode::kCbr);
    main_stream.rate_control_modes.push_back(RateControlMode::kVbr);
    main_stream.rate_control_modes.push_back(RateControlMode::kFixQp);
    main_stream.gop = GopRange{1, 120};
    main_stream.smart_codec_supported = true;
    capabilities.streams.push_back(main_stream);

    VideoStreamCapabilities sub_stream;
    sub_stream.stream_id = infra::StreamId::kSub;
    sub_stream.codecs.push_back(H264Capability());
    sub_stream.codecs.push_back(H265Capability());
    sub_stream.codecs.push_back(JpegCapability());
    sub_stream.codecs.push_back(MjpegCapability());
    sub_stream.resolutions.push_back(VideoResolution{1280, 720});
    sub_stream.resolutions.push_back(VideoResolution{704, 576});
    sub_stream.resolutions.push_back(VideoResolution{640, 360});
    sub_stream.resolutions.push_back(VideoResolution{352, 288});
    sub_stream.frame_rate = FrameRateRange{1, 30};
    sub_stream.bitrate = BitrateRange{64, 2048};
    sub_stream.rate_control_modes.push_back(RateControlMode::kCbr);
    sub_stream.rate_control_modes.push_back(RateControlMode::kVbr);
    sub_stream.rate_control_modes.push_back(RateControlMode::kFixQp);
    sub_stream.gop = GopRange{1, 120};
    sub_stream.smart_codec_supported = true;
    capabilities.streams.push_back(sub_stream);

    capabilities.image = DefaultImageCapabilities();
    return capabilities;
}

}  // namespace

infra::Result<MediaCapabilities> DefaultHisiSdk::GetCapabilities() {
    return infra::Result<MediaCapabilities>::Ok(DefaultCapabilities());
}

}  // namespace hisisdk
}  // namespace live_stream
