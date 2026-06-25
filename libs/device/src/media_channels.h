#ifndef LIVE_STREAM_DEVICE_SRC_MEDIA_CHANNELS_H_
#define LIVE_STREAM_DEVICE_SRC_MEDIA_CHANNELS_H_

#include "hisi_vendor/mpp_types.h"
#include "hisi_vendor/media_pipeline.h"

#include <cstdint>

namespace live_stream {
namespace device_internal {

const char *StreamName(StreamId stream_id);
const VideoStreamConfig *FindConfiguredStream(
    const MediaPipelineConfig &config, StreamId stream_id);
int32_t VencChannelForStream(const MediaPipelineConfig &config,
                             StreamId stream_id);
MediaChannels BuildChannelsForConfig(const MediaPipelineConfig &config);
bool IsValidSnapshotVencChannel(const MediaPipelineConfig &config);

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_MEDIA_CHANNELS_H_
