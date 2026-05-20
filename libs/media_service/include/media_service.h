#ifndef LIVE_STREAM_MEDIA_SERVICE_H_
#define LIVE_STREAM_MEDIA_SERVICE_H_

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

// IMediaView is the narrow interface consumed by HttpService (and other
// cross-module consumers). MediaService implements it so callers can depend on
// the interface rather than the concrete class.
class IMediaView {
public:
    virtual ~IMediaView() = default;
    virtual bool IsStarted() const = 0;
    virtual bool IsRestarting() const = 0;
    virtual bool IsStreamStarted(StreamId stream_id) const = 0;
    virtual MediaCapabilities GetCapabilities() const = 0;
};

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

class MediaService : public IMediaView, public IFrameSource {
public:
    MediaService();
    explicit MediaService(const MediaPipelineConfig& config);
    explicit MediaService(const MediaServiceOptions& options);
    ~MediaService();

    bool Start();
    void Stop();
    bool IsStarted() const override;
    bool IsRestarting() const override;
    bool IsStreamSupported(StreamId stream_id) const;
    bool IsStreamStarted(StreamId stream_id) const override;
    VideoCodec GetStreamCodec(StreamId stream_id) const override;

    static const char* StaticName();

    virtual FrameSubscriptionId SubscribeFrames(
        const FrameSubscribeOptions& options,
        IFrameSink* sink) override;
    virtual bool UnsubscribeFrames(FrameSubscriptionId subscription_id) override;
    virtual bool RequestKeyFrame(StreamId stream_id,
                                 KeyFrameReason reason) override;
    MediaCapabilities GetCapabilities() const override;

    bool SetEncodedFrameCallback(EncodedFrameCallback callback, void* user);
    MediaChannels GetChannels() const;
    MppChannel GetMainVpssChannel() const;
    MppChannel GetMainVencChannel() const;
    MediaServiceStats GetStats() const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SERVICE_H_
