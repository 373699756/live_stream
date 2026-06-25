#ifndef LIVE_STREAM_DEVICE_SRC_MEDIA_PIPELINE_H_
#define LIVE_STREAM_DEVICE_SRC_MEDIA_PIPELINE_H_

#include "media/frame_sink.h"
#include "media/stream_types.h"
#include "hisi_vendor/media_capabilities.h"
#include "hisi_vendor/mpp_types.h"
#include "hisi_vendor/media_pipeline.h"
#include "json.h"

namespace live_stream {

namespace hisisdk {
class IHisiImage;
class IHisiMediaPipeline;
class IHisiSystem;
class IHisiVencStream;
struct HisiSdk;
struct ExposureInfo;
}  // namespace hisisdk

bool IsValidMediaPipelineConfig(const MediaPipelineConfig& config);
bool IsValidMediaStream(StreamId stream_id);

class MediaPipeline {
public:
    MediaPipeline(MediaPipelineConfig config, hisisdk::HisiSdk sdk);

    const MediaPipelineConfig& config() const { return config_; }
    void SetConfig(const MediaPipelineConfig& config);
    void SetFrameCallback(MediaFrameCallback callback, void* user);

    MediaCapabilities GetCapabilities() const;
    bool InitSystem();
    bool DeinitSystem();
    bool Start();
    void Stop();
    bool ApplyVencRoi(StreamId stream_id);
    bool ApplyImageConfig(const Json& image_config);
    hisisdk::ExposureInfo QueryExposureInfo() const;

    bool system_initialized() const { return system_initialized_; }
    const MediaChannels& channels() const { return channels_; }

private:
    void BuildChannels();

    hisisdk::IHisiSystem *system_ = nullptr;
    hisisdk::IHisiMediaPipeline *media_pipeline_ = nullptr;
    hisisdk::IHisiVencStream *venc_stream_ = nullptr;
    hisisdk::IHisiImage *image_ = nullptr;
    MediaPipelineConfig config_;
    MediaFrameCallback frame_callback_ = nullptr;
    void* frame_callback_user_ = nullptr;
    MediaChannels channels_{};
    bool system_initialized_ = false;
    bool vi_started_ = false;
    bool vpss_started_ = false;
    bool vi_bound_vpss_ = false;
    bool venc_started_ = false;
    bool vpss_bound_venc_ = false;
    bool stream_started_ = false;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_MEDIA_PIPELINE_H_
