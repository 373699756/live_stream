#ifndef LIVE_STREAM_STREAM_HUB_SERVICE_H_
#define LIVE_STREAM_STREAM_HUB_SERVICE_H_

#include "stream_browser_source.h"

#include "media/frame_attach.h"
#include "media_service.h"

#include <cstdint>
#include <memory>

namespace live_stream {

struct StreamHubServiceOptions {
    uint32_t hls_segment_duration_ms = 1000;
    uint32_t hls_playlist_depth = 4;
    uint32_t hls_segment_retain_count = 2;
    uint32_t max_flv_clients = 8;
    uint32_t max_mjpeg_clients = 8;
    uint32_t max_frame_sinks = 8;
};

struct StreamHubServiceDependencies {
    IMediaService *media_service = nullptr;
};

class IStreamFrameSource {
public:
    virtual ~IStreamFrameSource() = default;

    virtual bool IsStreamAvailable(StreamId stream_id) const = 0;
    virtual VideoCodec GetStreamCodec(StreamId stream_id) const = 0;
    virtual FrameAttachId AttachFrameSink(
        const FrameAttachOptions &options, IFrameSink *sink) = 0;
    virtual bool DetachFrameSink(FrameAttachId sink_id) = 0;
    virtual bool RequestKeyFrame(StreamId stream_id,
                                 KeyFrameReason reason) = 0;
};

class IStreamHubService : public IStreamBrowserSource,
                          public IStreamFlvSource,
                          public IStreamMjpegSource,
                          public IStreamFrameSource {
public:
    ~IStreamHubService() override = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
};

std::unique_ptr<IStreamHubService>
CreateStreamHubService(const StreamHubServiceOptions &options,
                       const StreamHubServiceDependencies &dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_STREAM_HUB_SERVICE_H_
