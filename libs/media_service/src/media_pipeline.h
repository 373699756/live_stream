#ifndef LIVE_STREAM_MEDIA_SERVICE_SRC_MEDIA_PIPELINE_H_
#define LIVE_STREAM_MEDIA_SERVICE_SRC_MEDIA_PIPELINE_H_

#include "infra/status.h"
#include "infra/stream_types.h"
#include "media/media_capabilities.h"
#include "media/mpp_types.h"
#include "media/pipeline_config.h"

namespace live_stream {

bool IsValidMediaPipelineConfig(const MediaPipelineConfig& config);
bool IsValidMediaStream(infra::StreamId stream_id);

class MediaPipeline {
 public:
    explicit MediaPipeline(MediaPipelineConfig config);

    const MediaPipelineConfig& config() const { return config_; }

    infra::Result<MediaCapabilities> GetCapabilities() const;
    infra::Status InitSystem();
    void DeinitSystem();
    infra::Status Start();
    void Stop();

    bool system_initialized() const { return system_initialized_; }
    const MediaChannels& channels() const { return channels_; }

 private:
    void BuildChannels();

    MediaPipelineConfig config_;
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
