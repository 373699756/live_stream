#ifndef LIVE_STREAM_MEDIA_SERVICE_H_
#define LIVE_STREAM_MEDIA_SERVICE_H_

#include "media/frame_subscription.h"
#include "media/media_capabilities.h"
#include "media/mpp_types.h"
#include "media/pipeline_config.h"

#include <cstdint>
#include <memory>

namespace live_stream {

class IConfigService;

namespace hisisdk {
class IHisiSdk;
}  // namespace hisisdk

struct MediaServiceOptions {
    MediaPipelineConfig default_config;
    IConfigService* config_service = nullptr;
    hisisdk::IHisiSdk* sdk = nullptr;
};

class IMediaService {
public:
    virtual ~IMediaService() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsStarted() const = 0;
    virtual bool IsRestarting() const = 0;
    virtual bool IsStreamStarted(StreamId stream_id) const = 0;
    virtual VideoCodec GetStreamCodec(StreamId stream_id) const = 0;
    virtual FrameSubscriptionId SubscribeFrames(
        const FrameSubscribeOptions& options, IFrameSink* sink) = 0;
    virtual bool UnsubscribeFrames(FrameSubscriptionId subscription_id) = 0;
    virtual bool RequestKeyFrame(StreamId stream_id,
                                 KeyFrameReason reason) = 0;
    virtual MediaCapabilities GetCapabilities() const = 0;
    virtual MediaChannels GetChannels() const = 0;
};

std::unique_ptr<IMediaService> CreateMediaService();
std::unique_ptr<IMediaService> CreateMediaService(
    const MediaPipelineConfig& config);
std::unique_ptr<IMediaService> CreateMediaService(
    const MediaServiceOptions& options);

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SERVICE_H_
