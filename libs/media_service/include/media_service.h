#ifndef LIVE_STREAM_MEDIA_SERVICE_H_
#define LIVE_STREAM_MEDIA_SERVICE_H_

#include "infra/status.h"
#include "infra/service.h"
#include "media/frame_source.h"
#include "media/media_capabilities.h"
#include "media/mpp_types.h"
#include "media/pipeline_config.h"

#include <cstdint>

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

struct MediaServiceStats {
    uint64_t config_apply_count = 0;
    uint64_t config_apply_failed_count = 0;
    uint64_t restart_count = 0;
    uint32_t subscription_count = 0;
};

class MediaService : public infra::IService {
 public:
    MediaService();
    explicit MediaService(const MediaPipelineConfig& config);
    explicit MediaService(const MediaServiceOptions& options);
    ~MediaService() override;

    infra::Status Init() override;
    infra::Status Start() override;
    void Stop() override;
    void Deinit() override;
    const char* Name() const override;

    static const char* StaticName();

    virtual infra::Result<FrameSubscriptionId> SubscribeFrames(
        const FrameSubscribeOptions& options,
        IFrameSink* sink);
    virtual infra::Status UnsubscribeFrames(FrameSubscriptionId subscription_id);
    virtual infra::Status RequestKeyFrame(infra::StreamId stream_id,
                                          KeyFrameReason reason);
    infra::Result<MediaCapabilities> GetCapabilities() const;

    infra::Status SetEncodedFrameCallback(EncodedFrameCallback callback, void* user);
    infra::Result<MediaChannels> GetChannels() const;
    infra::Result<MppChannel> GetMainVpssChannel() const;
    infra::Result<MppChannel> GetMainVencChannel() const;
    MediaServiceStats GetStats() const;

 private:
    struct Impl;
    Impl* impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SERVICE_H_
