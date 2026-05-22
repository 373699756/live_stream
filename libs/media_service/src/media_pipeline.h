#ifndef LIVE_STREAM_MEDIA_SERVICE_SRC_MEDIA_PIPELINE_H_
#define LIVE_STREAM_MEDIA_SERVICE_SRC_MEDIA_PIPELINE_H_

#include "media/frame_attach.h"
#include "media/stream_types.h"
#include "media/media_capabilities.h"
#include "media/mpp_types.h"
#include "media/pipeline_config.h"
#include "live_stream/config_json.h"

namespace live_stream {

namespace hisisdk {
class IHisiSdk;
struct ExposureInfo;
}  // namespace hisisdk

bool IsValidMediaPipelineConfig(const MediaPipelineConfig& config);
bool IsValidMediaStream(StreamId stream_id);

class MediaPipeline {
public:
    explicit MediaPipeline(MediaPipelineConfig config);
    MediaPipeline(MediaPipelineConfig config, hisisdk::IHisiSdk* sdk);

    const MediaPipelineConfig& config() const { return config_; }
    void SetConfig(const MediaPipelineConfig& config);
    void SetFrameCallback(EncodedFrameCallback callback, void* user);

    MediaCapabilities GetCapabilities() const;
    bool InitSystem();
    void DeinitSystem();
    bool Start();
    void Stop();
    bool ApplyImageConfig(const ConfigJson& image_config);
    hisisdk::ExposureInfo QueryExposureInfo() const;

    bool system_initialized() const { return system_initialized_; }
    const MediaChannels& channels() const { return channels_; }

private:
    void BuildChannels();

    hisisdk::IHisiSdk* sdk_;
    MediaPipelineConfig config_;
    EncodedFrameCallback frame_callback_ = nullptr;
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

#endif  // LIVE_STREAM_MEDIA_SERVICE_SRC_MEDIA_PIPELINE_H_
