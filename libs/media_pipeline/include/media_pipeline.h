#ifndef LIVE_STREAM_MEDIA_PIPELINE_MEDIA_PIPELINE_H_
#define LIVE_STREAM_MEDIA_PIPELINE_MEDIA_PIPELINE_H_

#include "media_source.h"

#include "device_media.h"

#include <cstdint>
#include <memory>

namespace live_stream {

class IEvent;

struct MediaPipelineOptions {
    uint32_t hls_segment_duration_ms = 2000;
    uint32_t hls_playlist_depth = 3;
    uint32_t hls_segment_retain_count = 6;
    uint32_t max_flv_clients = 8;
    uint32_t max_mjpeg_clients = 8;
    uint32_t max_frame_readers = 8;
};

struct MediaPipelineDependencies {
    IDeviceMedia *device_media = nullptr;
    IEvent *event = nullptr;
};

class IMediaPipeline : public IMediaSource,
                            public IMediaFlvSource,
                            public IMediaMjpegSource,
                            public IMediaFrameSource {
public:
    ~IMediaPipeline() override = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
};

std::unique_ptr<IMediaPipeline>
CreateMediaPipeline(const MediaPipelineOptions &options,
                         const MediaPipelineDependencies &dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_PIPELINE_MEDIA_PIPELINE_H_
