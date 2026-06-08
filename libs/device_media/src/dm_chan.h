#ifndef LIVE_STREAM_DEVICE_MEDIA_SRC_DM_CHAN_H_
#define LIVE_STREAM_DEVICE_MEDIA_SRC_DM_CHAN_H_

#include "media/mpp_types.h"
#include "media/pipeline_config.h"

#include <cstdint>

namespace live_stream {
namespace device_media_internal {

const char *StreamName(StreamId stream_id);
const VideoStreamConfig *FindConfiguredStream(
    const MediaPipelineConfig &config, StreamId stream_id);
int32_t VencChannelForStream(const MediaPipelineConfig &config,
                             StreamId stream_id);
MediaChannels BuildChannelsForConfig(const MediaPipelineConfig &config);
bool IsValidSnapshotVencChannel(const MediaPipelineConfig &config);

}  // namespace device_media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_MEDIA_SRC_DM_CHAN_H_
