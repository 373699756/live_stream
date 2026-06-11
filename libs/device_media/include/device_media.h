#ifndef LIVE_STREAM_DEVICE_MEDIA_DEVICE_MEDIA_H_
#define LIVE_STREAM_DEVICE_MEDIA_DEVICE_MEDIA_H_

#include "media/frame_sink.h"
#include "media/media_capabilities.h"
#include "media/mpp_types.h"
#include "media/pipeline_config.h"

#include <cstdint>
#include <memory>
#include <string>

namespace live_stream {

class IConfig;

namespace hisisdk {
class IHisiSdk;
}  // namespace hisisdk

struct DeviceMediaOptions {
    MediaPipelineConfig default_config;
    IConfig* config = nullptr;
    hisisdk::IHisiSdk* sdk = nullptr;
};

struct ImageStrategyStatus {
    bool enabled = false;
    bool active = false;
    bool exposure_valid = false;
    uint32_t iso = 0;
    uint32_t exposure_time_us = 0;
    uint32_t analog_gain = 0;
    uint32_t digital_gain = 0;
    uint32_t isp_digital_gain = 0;
    std::string mode;
    std::string tier;
    int32_t saturation = 0;
    int32_t sharpness = 0;
    int32_t denoise_2d = 0;
    int32_t denoise_3d = 0;
    int32_t gamma = 0;
};

class IDeviceMedia {
public:
    virtual ~IDeviceMedia() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsStarted() const = 0;
    virtual bool IsRestarting() const = 0;
    virtual bool IsStreamStarted(StreamId stream_id) const = 0;
    virtual Codec GetStreamCodec(StreamId stream_id) const = 0;
    virtual bool SetFrameSink(FrameSink *sink) = 0;
    virtual bool RequestKeyFrame(StreamId stream_id,
                                 KeyFrameRequestType reason) = 0;
    virtual MediaCapabilities GetCapabilities() const = 0;
    virtual MediaChannels GetChannels() const = 0;
    virtual ImageStrategyStatus GetImageStrategyStatus() const = 0;
};

std::unique_ptr<IDeviceMedia> CreateDeviceMedia();
std::unique_ptr<IDeviceMedia> CreateDeviceMedia(
    const MediaPipelineConfig& config);
std::unique_ptr<IDeviceMedia> CreateDeviceMedia(
    const DeviceMediaOptions& options);

}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_MEDIA_DEVICE_MEDIA_H_
