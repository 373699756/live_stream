#ifndef LIVE_STREAM_MEDIA_MEDIA_SOURCE_REGISTRY_H_
#define LIVE_STREAM_MEDIA_MEDIA_SOURCE_REGISTRY_H_

namespace live_stream {

class MediaStreams;

class MediaSourceRegistry {
public:
    static bool Register(MediaStreams *media_streams);
    static MediaStreams *Streams();
    static void Clear(MediaStreams *media_streams);
    static void Clear();
};

}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_MEDIA_SOURCE_REGISTRY_H_
