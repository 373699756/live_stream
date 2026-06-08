#include "media_channels.h"

#include "hisisdk/hisi_sdk.h"

namespace live_stream {
namespace device_media_internal {

const char *StreamName(StreamId stream_id) {
    switch (stream_id) {
        case StreamId::kMain:
            return "main";
        case StreamId::kSub:
            return "sub";
        case StreamId::kSnapshot:
            return "snapshot";
    }
    return "unknown";
}

const VideoStreamConfig *FindConfiguredStream(
    const MediaPipelineConfig &config, StreamId stream_id) {
    if (stream_id == config.main_stream.stream_id) {
        return &config.main_stream;
    }
    if (stream_id == config.sub_stream.stream_id) {
        return &config.sub_stream;
    }
    return nullptr;
}

int32_t VencChannelForStream(const MediaPipelineConfig &config,
                             StreamId stream_id) {
    if (stream_id == config.sub_stream.stream_id) {
        return config.sub_venc_channel;
    }
    if (stream_id == config.main_stream.stream_id) {
        return config.venc_channel;
    }
    return -1;
}

MediaChannels BuildChannelsForConfig(const MediaPipelineConfig &config) {
    MediaChannels channels;
    channels.vi = MppChannel{MppModule::kVi, config.video_pipe,
                             config.vi_channel};
    channels.vpss = MppChannel{MppModule::kVpss, config.vpss_group,
                               config.vpss_channel};
    channels.sub_vpss = MppChannel{MppModule::kVpss, config.vpss_group,
                                   config.sub_vpss_channel};
    channels.venc = MppChannel{MppModule::kVenc, 0, config.venc_channel};
    channels.sub_venc =
        MppChannel{MppModule::kVenc, 0, config.sub_venc_channel};
    channels.video_pipe = config.video_pipe;
    channels.snap_pipe = config.snap_pipe;
    channels.main_size = config.main_stream.size;
    channels.sub_size = config.sub_stream.size;
    return channels;
}

bool IsValidSnapshotVencChannel(const MediaPipelineConfig &config) {
    const hisisdk::SnapshotConfig snapshot;
    if (snapshot.jpeg_venc_channel == config.venc_channel) {
        return false;
    }
    return !config.sub_stream.enabled ||
           snapshot.jpeg_venc_channel != config.sub_venc_channel;
}

}  // namespace device_media_internal
}  // namespace live_stream
