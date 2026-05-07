#ifndef LIVE_STREAM_WEB_MEDIA_SERVICE_H_
#define LIVE_STREAM_WEB_MEDIA_SERVICE_H_

#include "media/frame_source.h"
#include "media/stream_types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class MediaService;

using WebMediaFlvClientId = uint64_t;

struct WebMediaServiceOptions {
  uint32_t hls_segment_duration_ms = 1000;
  uint32_t hls_playlist_depth = 4;
  uint32_t max_flv_clients = 8;
};

struct WebMediaServiceDependencies {
  MediaService* media_service = nullptr;
};

struct WebMediaHlsEntry {
  uint64_t sequence = 0;
  int64_t duration_us = 0;
};

struct WebMediaHlsPlaylist {
  bool supported = false;
  uint32_t target_duration_sec = 0;
  uint64_t media_sequence = 0;
  std::vector<WebMediaHlsEntry> entries;
};

struct WebMediaSegment {
  bool found = false;
  uint64_t sequence = 0;
  int64_t duration_us = 0;
  std::string body;
};

struct WebMediaFlvBootstrap {
  bool supported = false;
  uint64_t config_generation = 0;
  std::string file_header;
  std::string sequence_header;
  std::string last_keyframe;
};

struct WebMediaServiceStats {
  bool enabled = false;
  uint64_t hls_segments_created = 0;
  uint32_t active_flv_clients = 0;
};

class IWebMediaFlvSink {
 public:
  virtual ~IWebMediaFlvSink() = default;

  virtual bool OnFlvChunk(const uint8_t* data, size_t size) = 0;
};

class IWebMediaService {
 public:
  virtual ~IWebMediaService() = default;

  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual bool IsHlsSupported(StreamId stream_id) const = 0;
  virtual bool IsFlvSupported(StreamId stream_id) const = 0;
  virtual WebMediaHlsPlaylist GetHlsPlaylist(StreamId stream_id) const = 0;
  virtual WebMediaSegment GetHlsSegment(StreamId stream_id,
                                        uint64_t sequence) const = 0;
  virtual WebMediaFlvBootstrap GetFlvBootstrap(StreamId stream_id) const = 0;
  virtual WebMediaFlvClientId AttachFlvClient(
      StreamId stream_id,
      uint64_t config_generation,
      const std::shared_ptr<IWebMediaFlvSink>& sink) = 0;
  virtual bool DetachFlvClient(WebMediaFlvClientId client_id) = 0;
  virtual WebMediaServiceStats GetStats() const = 0;
};

std::unique_ptr<IWebMediaService> CreateWebMediaService(
    const WebMediaServiceOptions& options,
    const WebMediaServiceDependencies& dependencies);

}  // namespace live_stream

#endif  // LIVE_STREAM_WEB_MEDIA_SERVICE_H_
