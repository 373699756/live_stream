// StubHisiSdk – host-side stub implementation of IHisiSdk.
//
// Every method returns a sensible default or performs minimal validation.
// No HiSilicon MPP SDK headers or calls are referenced here.
// For production MPP calls, use MppHisiSdk (libs/hisi_vendor).

#include "stub_hisi_sdk.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace hisisdk {
namespace {

// ── Capabilities helpers ──────────────────────────────────────
CodecCapability MakeCodecCap(VideoCodec codec,
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
        Range("saturation", 0, 100, 50),
        Range("sharpness", 0, 100, 50),
        Range("hue", 0, 100, 50),
    };
    img.exposure_options = {
        Options("mode", {"auto", "manual"}, "auto"),
        Options("anti_flicker", {"50hz", "60hz", "off"}, "50hz"),
    };
    img.white_balance_options = {
        Options("mode", {"auto", "manual", "indoor", "outdoor"}, "auto"),
    };
    img.white_balance_ranges = {
        Range("red_gain", 0, 100, 50),
        Range("blue_gain", 0, 100, 50),
    };
    img.mirror_supported = true;
    img.flip_supported = true;
    return img;
}

MediaCapabilities StubCapabilities() {
    MediaCapabilities caps;

    VideoStreamCapabilities main;
    main.stream_id = StreamId::kMain;
    main.codecs = {
        MakeCodecCap(VideoCodec::kH264, {"baseline", "main", "high"}),
        MakeCodecCap(VideoCodec::kH265, {"main"}),
        MakeCodecCap(VideoCodec::kJpeg, {"baseline"}),
        MakeCodecCap(VideoCodec::kMjpeg, {"baseline"}),
    };
    main.resolutions = {{3840, 2160}, {2560, 1440}, {1920, 1080}, {1280, 720}};
    main.frame_rate = {1, 30};
    main.bitrate = {512, 8192};
    main.rate_control_modes = {RateControlMode::kCbr, RateControlMode::kVbr,
                               RateControlMode::kFixQp};
    main.gop = {1, 120};
    main.smart_codec_supported = true;
    caps.streams.push_back(main);

    VideoStreamCapabilities sub;
    sub.stream_id = StreamId::kSub;
    sub.codecs = main.codecs;
    sub.resolutions = {{1280, 720}, {704, 576}, {640, 360}, {352, 288}};
    sub.frame_rate = {1, 30};
    sub.bitrate = {64, 2048};
    sub.rate_control_modes = main.rate_control_modes;
    sub.gop = {1, 120};
    sub.smart_codec_supported = true;
    caps.streams.push_back(sub);

    caps.image = DefaultImage();
    return caps;
}

// ── Snapshot stub helpers ─────────────────────────────────────
constexpr uint32_t kDefaultJpegPoolBlocks = 2;

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    uint32_t r = value % alignment;
    return r == 0 ? value : value + alignment - r;
}

uint32_t EstimateJpegBlockSize(const SnapshotConfig& config) {
    uint64_t pixels = static_cast<uint64_t>(config.size.width) *
                      config.size.height;
    uint64_t block_size = std::max(pixels, 1024ULL * 1024ULL);
    block_size = std::min(block_size, 16ULL * 1024ULL * 1024ULL);
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
    auto pool = CreateFixedMediaBufferPool(EstimateJpegBlockSize(config),
                                           kDefaultJpegPoolBlocks);
    if (!pool) return JpegFrame{};
    auto buffer = pool->Acquire();
    if (!buffer || buffer->Capacity() < sizeof(kMinimalJpeg)) return JpegFrame{};
    std::memcpy(buffer->MutableData(), kMinimalJpeg, sizeof(kMinimalJpeg));
    buffer->SetSize(static_cast<uint32_t>(sizeof(kMinimalJpeg)));
    JpegFrame frame;
    frame.buffer = buffer;
    frame.offset = 0;
    frame.size = buffer->Size();
    frame.width = config.size.width;
    frame.height = config.size.height;
    return frame;
}

}  // namespace

// ====================================================================
// StubHisiSdk – method implementations
// ====================================================================
MediaCapabilities StubHisiSdk::GetCapabilities() { return StubCapabilities(); }

bool StubHisiSdk::InitSystem(const MediaPipelineConfig& config) {
    return config.vb_block_count > 0;
}
void StubHisiSdk::DeinitSystem() {}

bool StubHisiSdk::StartVi(const MediaPipelineConfig& config) {
    return config.video_pipe >= 0;
}
void StubHisiSdk::StopVi(const MediaPipelineConfig&) {}

bool StubHisiSdk::StartVpss(const MediaPipelineConfig& config) {
    return config.vpss_group >= 0;
}
void StubHisiSdk::StopVpss(const MediaPipelineConfig&) {}

bool StubHisiSdk::BindViVpss(const MediaPipelineConfig& config) {
    return config.vi_channel >= 0;
}
void StubHisiSdk::UnbindViVpss(const MediaPipelineConfig&) {}

bool StubHisiSdk::StartVenc(const MediaPipelineConfig& config) {
    if (config.venc_channel < 0) return false;
    return !config.sub_stream.enabled || config.sub_venc_channel >= 0;
}
void StubHisiSdk::StopVenc(const MediaPipelineConfig&) {}

bool StubHisiSdk::BindVpssVenc(const MediaPipelineConfig& config) {
    return config.vpss_channel >= 0 &&
           (!config.sub_stream.enabled || config.sub_vpss_channel >= 0);
}
void StubHisiSdk::UnbindVpssVenc(const MediaPipelineConfig&) {}

bool StubHisiSdk::StartVencStream(const MediaPipelineConfig& config,
                                  EncodedFrameCallback, void*) {
    return config.main_stream.bitrate_kbps > 0 &&
           (!config.sub_stream.enabled || config.sub_stream.bitrate_kbps > 0);
}
void StubHisiSdk::StopVencStream(const MediaPipelineConfig&) {}

bool StubHisiSdk::RequestIdr(int32_t venc_channel) {
    return venc_channel >= 0;
}

bool StubHisiSdk::ApplyImageConfig(const MediaPipelineConfig& config,
                                   const ConfigJson& image_config) {
    return config.video_pipe >= 0 && config.vi_channel >= 0 &&
           image_config.is_object();
}

bool StubHisiSdk::CreateRegion(int32_t handle, const RegionConfig& config) {
    (void)config;
    return handle >= 0 && config.size.width > 0 && config.size.height > 0;
}
bool StubHisiSdk::AttachRegion(int32_t handle, const RegionConfig&) {
    return handle >= 0;
}
bool StubHisiSdk::DetachRegion(int32_t handle, const RegionConfig&) {
    return handle >= 0;
}
bool StubHisiSdk::SetRegionDisplay(int32_t handle, const RegionConfig&) {
    return handle >= 0;
}
bool StubHisiSdk::SetRegionBitmap(int32_t handle, const Bitmap& bitmap) {
    return handle >= 0 && bitmap.data != nullptr && bitmap.size > 0;
}
void StubHisiSdk::DestroyRegion(int32_t) {}

JpegFrame StubHisiSdk::CaptureJpeg(const SnapshotConfig& config) {
    if (config.size.width == 0 || config.size.height == 0 ||
        config.timeout_ms == 0) {
        return JpegFrame{};
    }
    return MakeHostJpeg(config);
}

// ====================================================================
// Factory – returns a static StubHisiSdk instance.
// ====================================================================
IHisiSdk& DefaultSdk() {
    static StubHisiSdk sdk;
    return sdk;
}

}  // namespace hisisdk
}  // namespace live_stream
