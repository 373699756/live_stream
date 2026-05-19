#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace hisisdk {
namespace {

// ─── Static capability helpers (from SDK compile-time constants) ─
CodecCapability H264Capability() {
    CodecCapability cap;
    cap.codec = VideoCodec::kH264;
    cap.profiles = {"baseline", "main", "high"};
    return cap;
}

CodecCapability H265Capability() {
    CodecCapability cap;
    cap.codec = VideoCodec::kH265;
    cap.profiles = {"main", "main10"};
    return cap;
}

CodecCapability MjpegCapability() {
    CodecCapability cap;
    cap.codec = VideoCodec::kMjpeg;
    cap.profiles = {"baseline"};
    return cap;
}

NumericControlCapability Range(const char* name, int32_t min, int32_t max,
                               int32_t default_value,
                               bool runtime_supported = true) {
    NumericControlCapability cap;
    cap.name = name;
    cap.min = min;
    cap.max = max;
    cap.default_value = default_value;
    cap.runtime_supported = runtime_supported;
    return cap;
}

OptionControlCapability Options(const char* name,
                                std::vector<std::string> values,
                                const char* default_value,
                                bool runtime_supported = true) {
    OptionControlCapability cap;
    cap.name = name;
    cap.values = std::move(values);
    cap.default_value = default_value;
    cap.runtime_supported = runtime_supported;
    return cap;
}

void AddCommonCodecs(std::vector<CodecCapability>& codecs) {
    codecs.push_back(H264Capability());
    codecs.push_back(H265Capability());
    codecs.push_back(MjpegCapability());
}

void AddCommonRcModes(std::vector<RateControlMode>& modes) {
    modes.push_back(RateControlMode::kCbr);
    modes.push_back(RateControlMode::kVbr);
    modes.push_back(RateControlMode::kFixQp);
}

ImageCapabilities DefaultImageCapabilities() {
    ImageCapabilities image;

    image.basic.push_back(Range("brightness", 0, 100, 50, false));
    image.basic.push_back(Range("contrast", 0, 100, 50, false));
    image.basic.push_back(Range("saturation", 0, 100, 50));
    image.basic.push_back(Range("sharpness", 0, 100, 50));
    image.basic.push_back(Range("hue", 0, 100, 50, false));

    image.exposure_options.push_back(
        Options("mode", {"auto", "manual"}, "auto"));
    image.exposure_options.push_back(
        Options("anti_flicker", {"50hz", "60hz", "off"}, "50hz"));
    image.exposure_options.push_back(
        Options("exposure_time",
                {"auto", "1/25", "1/50", "1/100", "1/250"}, "auto"));
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
        Options("mode", {"color", "black_white", "auto"}, "color", false));
    image.mirror_supported = true;
    image.flip_supported = true;

    return image;
}

#ifdef LIVE_STREAM_ENABLE_HISI_MPP

// ─── Capabilities that can be refined at runtime ───────────────
VideoStreamCapabilities BuildMainStreamCaps() {
    VideoStreamCapabilities main;
    main.stream_id = StreamId::kMain;
    AddCommonCodecs(main.codecs);
    main.resolutions = {
        {3840, 2160},  // 4K
        {2560, 1440},  // 2K
        {1920, 1080},  // 1080P
        {1280, 720},   // 720P
        {704, 576},    // D1
        {640, 360},    // 360P
        {352, 288},    // CIF
    };
    main.frame_rate = FrameRateRange{1, 30};
    main.bitrate = BitrateRange{MIN_BITRATE, MAX_BITRATE};
    AddCommonRcModes(main.rate_control_modes);
    main.gop = GopRange{1, 120};
    main.smart_codec_supported = true;
    return main;
}

VideoStreamCapabilities BuildSubStreamCaps() {
    VideoStreamCapabilities sub;
    sub.stream_id = StreamId::kSub;
    AddCommonCodecs(sub.codecs);
    sub.resolutions = {
        {1280, 720},
        {704, 576},
        {640, 360},
        {352, 288},
    };
    sub.frame_rate = FrameRateRange{1, 30};
    sub.bitrate = BitrateRange{MIN_BITRATE, 2048};
    AddCommonRcModes(sub.rate_control_modes);
    sub.gop = GopRange{1, 120};
    sub.smart_codec_supported = true;
    return sub;
}

#endif  // LIVE_STREAM_ENABLE_HISI_MPP

}  // anonymous namespace

// ====================================================================
// GetCapabilities
// ====================================================================
MediaCapabilities MppHisiSdk::GetCapabilities() {
    MediaCapabilities caps;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    // Build from SDK compile-time macro constants
    caps.streams.push_back(BuildMainStreamCaps());
    caps.streams.push_back(BuildSubStreamCaps());

    // In future: if system is initialised and sensor info is available,
    // refine the resolution list and frame-rate limits here using
    // SAMPLE_COMM_VI_GetSizeBySensor / GetFrameRateBySensor.
#else
    // Fallback: same as StubHisiSdk
    VideoStreamCapabilities main, sub;
    main.stream_id = StreamId::kMain;
    AddCommonCodecs(main.codecs);
    main.resolutions = {{3840, 2160}, {2560, 1440}, {1920, 1080}, {1280, 720}};
    main.frame_rate = {1, 30};
    main.bitrate = {512, 8192};
    AddCommonRcModes(main.rate_control_modes);
    main.gop = {1, 120};
    main.smart_codec_supported = true;
    caps.streams.push_back(main);

    sub.stream_id = StreamId::kSub;
    AddCommonCodecs(sub.codecs);
    sub.resolutions = {{1280, 720}, {704, 576}, {640, 360}, {352, 288}};
    sub.frame_rate = {1, 30};
    sub.bitrate = {64, 2048};
    AddCommonRcModes(sub.rate_control_modes);
    sub.gop = {1, 120};
    sub.smart_codec_supported = true;
    caps.streams.push_back(sub);
#endif

    caps.image = DefaultImageCapabilities();
    return caps;
}

}  // namespace hisisdk
}  // namespace live_stream
