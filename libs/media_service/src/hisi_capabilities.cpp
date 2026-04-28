#include "hisi_sdk_default.h"

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

MediaCapabilities DefaultCapabilities() {
    MediaCapabilities capabilities;

    VideoStreamCapabilities main_stream;
    main_stream.stream_id = infra::StreamId::kMain;
    main_stream.codecs.push_back(H264Capability());
    main_stream.codecs.push_back(H265Capability());
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

    return capabilities;
}

}  // namespace

infra::Result<MediaCapabilities> DefaultHisiSdk::GetCapabilities() {
    return infra::Result<MediaCapabilities>::Ok(DefaultCapabilities());
}

}  // namespace hisisdk
}  // namespace live_stream
