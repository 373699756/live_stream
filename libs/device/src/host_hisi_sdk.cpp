// Host-side default implementation of the narrow HiSilicon SDK APIs.
//
// Every method returns a sensible default or performs minimal validation.
// No HiSilicon MPP SDK headers or calls are referenced here.
// For production MPP calls, use MppHisiSdk (libs/hisi_vendor).

#include "host_hisi_sdk.h"

#include "infra/clamp.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace hisisdk {

class HostHisiSdk final : public IHisiSystem,
                          public IHisiMediaPipeline,
                          public IHisiVencStream,
                          public IHisiRegion,
                          public IHisiSnapshot,
                          public IHisiImage {
public:
    HostHisiSdk() = default;

    MediaCapabilities GetCapabilities() override;
    bool InitSystem(const MediaPipelineConfig& config) override;
    bool DeinitSystem() override;
    bool StartVi(const MediaPipelineConfig& config) override;
    void StopVi(const MediaPipelineConfig& config) override;
    bool StartVpss(const MediaPipelineConfig& config) override;
    void StopVpss(const MediaPipelineConfig& config) override;
    bool BindViVpss(const MediaPipelineConfig& config) override;
    void UnbindViVpss(const MediaPipelineConfig& config) override;
    bool StartVenc(const MediaPipelineConfig& config) override;
    void StopVenc(const MediaPipelineConfig& config) override;
    bool BindVpssVenc(const MediaPipelineConfig& config) override;
    void UnbindVpssVenc(const MediaPipelineConfig& config) override;
    bool StartVencStream(const MediaPipelineConfig& config,
                         MediaFrameCallback callback,
                         void* user) override;
    void StopVencStream(const MediaPipelineConfig& config) override;
    bool RequestIdr(int32_t venc_channel) override;
    bool ApplyVencRoi(int32_t venc_channel,
                      const VideoStreamConfig& stream_config) override;
    bool ApplyImageConfig(const MediaPipelineConfig& config,
                          const Json& image_config) override;
    ExposureInfo QueryExposureInfo(
        const MediaPipelineConfig& config) override;
    bool CreateRegion(int32_t handle,
                      const RegionConfig& config) override;
    bool AttachRegion(int32_t handle,
                      const RegionConfig& config) override;
    bool DetachRegion(int32_t handle,
                      const RegionConfig& config) override;
    bool SetRegionDisplay(int32_t handle,
                          const RegionConfig& config) override;
    bool SetRegionBitmap(int32_t handle,
                         const Bitmap& bitmap) override;
    void DestroyRegion(int32_t handle) override;
    JpegFrame CaptureJpeg(const SnapshotConfig& config) override;
    YuvFrame CaptureYuvFrame(const MppChannel& vpss_channel,
                             Size size,
                             uint32_t timeout_ms) override;
};

namespace {

CodecCapability MakeCodecCap(Codec codec,
                             std::vector<std::string> profiles) {
    CodecCapability cap;
    cap.codec = codec;
    cap.profiles = std::move(profiles);
    return cap;
}

NumericControlCapability Range(const char* name,
                               int32_t min,
                               int32_t max,
                               int32_t dv) {
    NumericControlCapability cap;
    cap.name = name;
    cap.min = min;
    cap.max = max;
    cap.default_value = dv;
    return cap;
}

OptionControlCapability Options(const char* name,
                                std::vector<std::string> values,
                                const char* dv) {
    OptionControlCapability cap;
    cap.name = name;
    cap.values = std::move(values);
    cap.default_value = dv;
    return cap;
}

ImageCapabilities DefaultImage() {
    ImageCapabilities img;
    img.basic = {
        Range("brightness", 0, 100, 50),
        Range("contrast", 0, 100, 50),
        Range("saturation", 0, 100, 52),
        Range("sharpness", 0, 100, 32),
        Range("hue", 0, 100, 50),
    };
    img.exposure_options = {
        Options("mode", {"auto", "manual"}, "auto"),
        Options("anti_flicker", {"50hz", "60hz", "off"}, "50hz"),
        Options("exposure_time",
                {"auto", "1/12", "1/25", "1/30", "1/50", "1/100",
                 "1/250"},
                "auto"),
        Options("max_exposure_time", {"1/12", "1/25", "1/30", "1/50", "1/100", "1/250"}, "1/30"),
        Options("gain", {"auto", "low", "medium", "high"}, "auto"),
        Options("slow_shutter", {"false", "true"}, "false"),
    };
    img.exposure_ranges = {
        Range("compensation", 0, 100, 50),
    };
    img.white_balance_options = {
        Options("mode", {"auto", "manual"}, "auto"),
    };
    img.white_balance_ranges = {
        Range("red_gain", 0, 100, 50),
        Range("blue_gain", 0, 100, 50),
    };
    img.enhancement_options = {
        Options("defog", {"false", "true"}, "false"),
    };
    img.enhancement_ranges = {
        Range("denoise_2d", 0, 100, 60),
        Range("denoise_3d", 0, 100, 52),
        Range("gamma", 0, 100, 50),
    };
    img.backlight_options = {
        Options("mode", {"off", "drc"}, "off"),
    };
    img.backlight_ranges = {
        Range("level", 0, 100, 50),
    };
    img.color_mode_options = {
        Options("mode", {"color", "black_white"}, "color"),
    };
    img.lens_correction_options = {
        Options("aspect", {"false", "true"}, "true"),
    };
    img.lens_correction_ranges = {
        Range("x_ratio", 0, 100, 100),
        Range("y_ratio", 0, 100, 100),
        Range("xy_ratio", 0, 100, 100),
        Range("center_x_offset", -511, 511, 0),
        Range("center_y_offset", -511, 511, 0),
        Range("distortion_ratio", -300, 500, 0),
    };
    img.lens_correction_supported = true;
    img.lens_correction_min_width = 640;
    img.lens_correction_min_height = 480;
    img.stabilization_options = {
        Options("motion_level", {"low", "normal", "high"}, "normal"),
    };
    img.stabilization_ranges = {
        Range("crop_ratio", 50, 98, 80),
        Range("buffer_frames", 5, 10, 6),
        Range("frame_rate", 1, 60, 30),
        Range("moving_subject_level", 0, 6, 0),
        Range("rolling_shutter_coef", 0, 1000, 0),
        Range("horizontal_limit", 0, 1000, 512),
        Range("vertical_limit", 0, 1000, 512),
    };
    img.stabilization_supported = true;
    img.stabilization_min_width = 1280;
    img.stabilization_min_height = 720;
    img.mirror_supported = true;
    img.flip_supported = true;
    return img;
}

MediaCapabilities HostCapabilities() {
    MediaCapabilities caps;

    VideoStreamCapabilities main;
    main.stream_id = StreamId::kMain;
    main.codecs = {
        MakeCodecCap(Codec::kH264, {"baseline", "main", "high"}),
        MakeCodecCap(Codec::kH265, {"main"}),
        MakeCodecCap(Codec::kMjpeg, {}),
    };
    main.resolutions = {{1920, 1080}, {1280, 720}, {704, 576}, {640, 360}, {352, 288}};
    main.frame_rate = {1, 30};
    main.bitrate = {512, 12288};
    main.rate_control_modes = {RateControlMode::kCbr, RateControlMode::kVbr,
                               RateControlMode::kFixQp};
    main.gop = {1, 120};
    main.smart_codec_supported = true;
    main.roi_supported = true;
    main.max_roi_regions = 8;
    caps.streams.push_back(main);

    VideoStreamCapabilities sub;
    sub.stream_id = StreamId::kSub;
    sub.codecs = main.codecs;
    sub.resolutions = {{1280, 720}, {704, 576}, {640, 360}, {352, 288}};
    sub.frame_rate = {1, 30};
    sub.bitrate = {64, 4096};
    sub.rate_control_modes = main.rate_control_modes;
    sub.gop = {1, 120};
    sub.smart_codec_supported = true;
    sub.roi_supported = true;
    sub.max_roi_regions = 8;
    caps.streams.push_back(sub);

    caps.image = DefaultImage();
    return caps;
}

// ── Snapshot image helpers ────────────────────────────────────
constexpr uint32_t kDefaultJpegPoolBlocks = 2;
constexpr uint64_t kMinJpegBlockSize = 1024ULL * 1024ULL;
constexpr uint64_t kMaxJpegBlockSize = 16ULL * 1024ULL * 1024ULL;

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    uint32_t r = value % alignment;
    return r == 0 ? value : value + alignment - r;
}

uint32_t EstimateJpegBlockSize(const SnapshotConfig& config) {
    const uint64_t pixels = static_cast<uint64_t>(config.size.width) *
                            config.size.height;
    const uint64_t block_size =
        infra::Clamp(pixels, kMinJpegBlockSize, kMaxJpegBlockSize);
    return AlignUp(static_cast<uint32_t>(block_size), 4096);
}

JpegFrame MakeHostJpeg(const SnapshotConfig& config) {
    static const uint8_t kMinimalJpeg[] = {
        0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01,
        0x01, 0x01, 0x00, 0x48, 0x00, 0x48, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43,
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x01, 0x00,
        0x01, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xff,
        0xc4, 0x00, 0x15, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0xff, 0xc4,
        0x00, 0x15, 0x11, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0xff, 0xda,
        0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x10, 0x03, 0x10, 0x00, 0x00, 0x00,
        0x8f, 0xff, 0xc4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
        0xff, 0xda, 0x00, 0x08, 0x01, 0x01, 0x00, 0x01, 0x3f, 0x00, 0x7f, 0xff,
        0xc4, 0x00, 0x14, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0xff, 0xda,
        0x00, 0x08, 0x01, 0x02, 0x01, 0x01, 0x3f, 0x00, 0x7f, 0xff, 0xc4, 0x00,
        0x14, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0xff, 0xda, 0x00, 0x08,
        0x01, 0x03, 0x01, 0x01, 0x3f, 0x00, 0x7f, 0xff, 0xd9};
    auto pool = CreateMediaBufferPool(EstimateJpegBlockSize(config),
                                      kDefaultJpegPoolBlocks);
    if (!pool) return JpegFrame{};
    MediaBufferBuilder buffer = pool->Acquire();
    if (buffer.Capacity() < sizeof(kMinimalJpeg)) {
        return JpegFrame{};
    }
    std::memcpy(buffer.Data(), kMinimalJpeg, sizeof(kMinimalJpeg));
    (void)buffer.Resize(static_cast<uint32_t>(sizeof(kMinimalJpeg)));
    JpegFrame frame;
    frame.buffer = buffer.Finish();
    frame.width = config.size.width;
    frame.height = config.size.height;
    return frame;
}

}  // namespace

// ====================================================================
// HostHisiSdk – method implementations
// ====================================================================
MediaCapabilities HostHisiSdk::GetCapabilities() { return HostCapabilities(); }

bool HostHisiSdk::InitSystem(const MediaPipelineConfig& config) {
    return config.vb_blocks > 0;
}
bool HostHisiSdk::DeinitSystem() { return true; }

bool HostHisiSdk::StartVi(const MediaPipelineConfig& config) {
    return config.video_pipe >= 0;
}
void HostHisiSdk::StopVi(const MediaPipelineConfig&) {}

bool HostHisiSdk::StartVpss(const MediaPipelineConfig& config) {
    return config.vpss_group >= 0;
}
void HostHisiSdk::StopVpss(const MediaPipelineConfig&) {}

bool HostHisiSdk::BindViVpss(const MediaPipelineConfig& config) {
    return config.vi_channel >= 0;
}
void HostHisiSdk::UnbindViVpss(const MediaPipelineConfig&) {}

bool HostHisiSdk::StartVenc(const MediaPipelineConfig& config) {
    if (config.venc_channel < 0) return false;
    return !config.sub_stream.enabled || config.sub_venc_channel >= 0;
}
void HostHisiSdk::StopVenc(const MediaPipelineConfig&) {}

bool HostHisiSdk::BindVpssVenc(const MediaPipelineConfig& config) {
    return config.vpss_channel >= 0 &&
           (!config.sub_stream.enabled || config.sub_vpss_channel >= 0);
}
void HostHisiSdk::UnbindVpssVenc(const MediaPipelineConfig&) {}

bool HostHisiSdk::StartVencStream(const MediaPipelineConfig& config,
                                  MediaFrameCallback, void*) {
    return config.main_stream.bitrate_kbps > 0 &&
           (!config.sub_stream.enabled || config.sub_stream.bitrate_kbps > 0);
}
void HostHisiSdk::StopVencStream(const MediaPipelineConfig&) {}

bool HostHisiSdk::RequestIdr(int32_t venc_channel) {
    return venc_channel >= 0;
}

bool HostHisiSdk::ApplyVencRoi(int32_t venc_channel,
                               const VideoStreamConfig& stream_config) {
    if (venc_channel < 0) {
        return false;
    }
    if (!stream_config.roi.enabled) {
        return true;
    }
    if (stream_config.roi.regions.size() > 8) {
        return false;
    }
    for (const VideoRoiRegion& region : stream_config.roi.regions) {
        if (region.enabled && (region.width == 0 || region.height == 0)) {
            return false;
        }
    }
    return true;
}

bool HostHisiSdk::ApplyImageConfig(const MediaPipelineConfig& config,
                                   const Json& image_config) {
    return config.video_pipe >= 0 && config.vi_channel >= 0 &&
           image_config.is_object();
}

ExposureInfo HostHisiSdk::QueryExposureInfo(const MediaPipelineConfig& config) {
    ExposureInfo info;
    if (config.video_pipe < 0) {
        return info;
    }
    info.valid = true;
    info.exposure_time_us = 10000;
    info.analog_gain = 0x400;
    info.digital_gain = 0x400;
    info.isp_digital_gain = 0x400;
    info.iso = 200;
    return info;
}

bool HostHisiSdk::CreateRegion(int32_t handle, const RegionConfig& config) {
    (void)config;
    return handle >= 0 && config.size.width > 0 && config.size.height > 0;
}
bool HostHisiSdk::AttachRegion(int32_t handle, const RegionConfig&) {
    return handle >= 0;
}
bool HostHisiSdk::DetachRegion(int32_t handle, const RegionConfig&) {
    return handle >= 0;
}
bool HostHisiSdk::SetRegionDisplay(int32_t handle, const RegionConfig&) {
    return handle >= 0;
}
bool HostHisiSdk::SetRegionBitmap(int32_t handle, const Bitmap& bitmap) {
    return handle >= 0 && bitmap.data != nullptr && bitmap.size > 0;
}
void HostHisiSdk::DestroyRegion(int32_t) {}

JpegFrame HostHisiSdk::CaptureJpeg(const SnapshotConfig& config) {
    if (config.size.width == 0 || config.size.height == 0 ||
        config.timeout_ms == 0) {
        return JpegFrame{};
    }
    return MakeHostJpeg(config);
}

YuvFrame HostHisiSdk::CaptureYuvFrame(const MppChannel& vpss_channel,
                                      Size size,
                                      uint32_t timeout_ms) {
    if (vpss_channel.module != MppModule::kVpss || size.width == 0 ||
        size.height == 0 || timeout_ms == 0) {
        return YuvFrame{};
    }
    const uint32_t y_size = size.width * size.height;
    const uint32_t uv_size = y_size / 2;
    MediaBufferBuilder buffer = MediaBufferBuilder::Allocate(y_size + uv_size);
    uint8_t* buffer_data = buffer.Data();
    if (buffer_data == nullptr) {
        return YuvFrame{};
    }
    std::memset(buffer_data, 0x10, y_size);
    std::memset(buffer_data + y_size, 0x80, uv_size);
    (void)buffer.Resize(y_size + uv_size);
    YuvFrame frame;
    frame.buffer = buffer.Finish();
    frame.width = size.width;
    frame.height = size.height;
    frame.stride_y = size.width;
    frame.stride_uv = size.width;
    return frame;
}

// ====================================================================
}  // namespace hisisdk

namespace device_internal {

hisisdk::HisiSdk HostHisiSdk() {
    static hisisdk::HostHisiSdk sdk;
    return hisisdk::HisiSdk{&sdk, &sdk, &sdk, &sdk, &sdk, &sdk};
}

}  // namespace device_internal
}  // namespace live_stream
