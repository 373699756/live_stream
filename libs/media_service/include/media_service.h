#ifndef LIVE_STREAM_MEDIA_SERVICE_H_
#define LIVE_STREAM_MEDIA_SERVICE_H_

#include "infra/status.h"
#include "infra/service.h"
#include "media/frame_source.h"
#include "media/media_capabilities.h"
#include "media/mpp_types.h"
#include "media/pipeline_config.h"

namespace live_stream {

class MediaService : public infra::IService {
 public:
    MediaService();
    explicit MediaService(const MediaPipelineConfig& config);
    ~MediaService() override;

    infra::Status Init() override;
    infra::Status Start() override;
    void Stop() override;
    void Deinit() override;
    const char* Name() const override;

    static const char* StaticName();

    infra::Result<FrameSubscriptionId> SubscribeFrames(
        const FrameSubscribeOptions& options,
        IFrameSink* sink);
    infra::Status UnsubscribeFrames(FrameSubscriptionId subscription_id);
    infra::Status RequestKeyFrame(infra::StreamId stream_id,
                                 KeyFrameReason reason);
    infra::Result<MediaCapabilities> GetCapabilities() const;

    infra::Status SetEncodedFrameCallback(EncodedFrameCallback callback, void* user);
    infra::Result<MediaChannels> GetChannels() const;
    infra::Result<MppChannel> GetMainVpssChannel() const;
    infra::Result<MppChannel> GetMainVencChannel() const;

 private:
    struct Impl;
    Impl* impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SERVICE_H_
