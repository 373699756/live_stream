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

    image.basic.push_back(Range("brightness", 0, 100, 50));
    image.basic.push_back(Range("contrast", 0, 100, 50));
    image.basic.push_back(Range("saturation", 0, 100, 52));
    image.basic.push_back(Range("sharpness", 0, 100, 32));
    image.basic.push_back(Range("hue", 0, 100, 50));

    image.exposure_options.push_back(
        Options("mode", {"auto", "manual"}, "auto"));
    image.exposure_options.push_back(
        Options("anti_flicker", {"50hz", "60hz", "off"}, "50hz"));
    image.exposure_options.push_back(
        Options("exposure_time",
                {"auto", "1/12", "1/25", "1/30", "1/50", "1/100",
                 "1/250"},
                "auto"));
    image.exposure_options.push_back(
        Options("max_exposure_time", {"1/12", "1/25", "1/30", "1/50",
                                      "1/100", "1/250"}, "1/30"));
    image.exposure_options.push_back(
        Options("gain", {"auto", "low", "medium", "high"}, "auto"));
    image.exposure_options.push_back(
        Options("slow_shutter", {"false", "true"}, "false"));
    image.exposure_ranges.push_back(Range("compensation", 0, 100, 50));

    image.white_balance_options.push_back(
        Options("mode", {"auto", "manual"}, "auto"));
    image.white_balance_ranges.push_back(Range("red_gain", 0, 100, 50));
    image.white_balance_ranges.push_back(Range("blue_gain", 0, 100, 50));

    image.enhancement_ranges.push_back(Range("denoise_2d", 0, 100, 60));
    image.enhancement_ranges.push_back(Range("denoise_3d", 0, 100, 52));
    image.enhancement_ranges.push_back(Range("gamma", 0, 100, 50));
    image.enhancement_options.push_back(
        Options("defog", {"false", "true"}, "false"));

    image.backlight_options.push_back(
        Options("mode", {"off", "drc"}, "off"));
    image.backlight_ranges.push_back(Range("level", 0, 100, 50));
    image.color_mode_options.push_back(
        Options("mode", {"color", "black_white"}, "color"));
    image.lens_correction_options.push_back(
        Options("aspect", {"false", "true"}, "true"));
    image.lens_correction_ranges.push_back(Range("x_ratio", 0, 100, 100));
    image.lens_correction_ranges.push_back(Range("y_ratio", 0, 100, 100));
    image.lens_correction_ranges.push_back(Range("xy_ratio", 0, 100, 100));
    image.lens_correction_ranges.push_back(
        Range("center_x_offset", -511, 511, 0));
    image.lens_correction_ranges.push_back(
        Range("center_y_offset", -511, 511, 0));
    image.lens_correction_ranges.push_back(
        Range("distortion_ratio", -300, 500, 0));
    image.lens_correction_supported = true;
    image.lens_correction_min_width = LDC_MIN_IMAGE_WIDTH;
    image.lens_correction_min_height = LDC_MIN_IMAGE_HEIGHT;
    image.stabilization_options.push_back(
        Options("motion_level", {"low", "normal", "high"}, "normal"));
    image.stabilization_ranges.push_back(Range("crop_ratio", 50, 98, 80));
    image.stabilization_ranges.push_back(Range("buffer_count", 5, 10, 6));
    image.stabilization_ranges.push_back(Range("frame_rate", 1, 60, 30));
    image.stabilization_ranges.push_back(
        Range("moving_subject_level", 0, 6, 0));
    image.stabilization_ranges.push_back(
        Range("rolling_shutter_coef", 0, 1000, 0));
    image.stabilization_ranges.push_back(
        Range("horizontal_limit", 0, 1000, 512));
    image.stabilization_ranges.push_back(
        Range("vertical_limit", 0, 1000, 512));
    image.stabilization_supported = true;
    image.stabilization_min_width = DIS_MIN_IMAGE_WIDTH;
    image.stabilization_min_height = DIS_MIN_IMAGE_HEIGHT;
    image.mirror_supported = true;
    image.flip_supported = true;

    return image;
}

// ─── Capabilities that can be refined at runtime ───────────────
VideoStreamCapabilities BuildMainStreamCaps() {
    VideoStreamCapabilities main;
    main.stream_id = StreamId::kMain;
    AddCommonCodecs(main.codecs);
    main.resolutions = {
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
    main.roi_supported = true;
    main.max_roi_regions = VENC_MAX_ROI_NUM;
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
    sub.bitrate = BitrateRange{MIN_BITRATE, 4096};
    AddCommonRcModes(sub.rate_control_modes);
    sub.gop = GopRange{1, 120};
    sub.smart_codec_supported = true;
    sub.roi_supported = true;
    sub.max_roi_regions = VENC_MAX_ROI_NUM;
    return sub;
}

}  // anonymous namespace

// ====================================================================
// GetCapabilities
// ====================================================================
MediaCapabilities MppHisiSdk::GetCapabilities() {
    MediaCapabilities caps;

    // Build from SDK compile-time macro constants
    caps.streams.push_back(BuildMainStreamCaps());
    caps.streams.push_back(BuildSubStreamCaps());

    // In future: if system is initialised and sensor info is available,
    // refine the resolution list and frame-rate limits here using
    // SAMPLE_COMM_VI_GetSizeBySensor / GetFrameRateBySensor.

    caps.image = DefaultImageCapabilities();
    return caps;
}

}  // namespace hisisdk
}  // namespace live_stream
