#ifndef LIVE_STREAM_FRAME_SERVICE_H_
#define LIVE_STREAM_FRAME_SERVICE_H_

#include "media/frame_source.h"
#include "media/stream_types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

class MediaService;

using FrameFlvClientId = uint64_t;

struct FrameServiceOptions {
  uint32_t hls_segment_duration_ms = 1000;
  uint32_t hls_playlist_depth = 4;
  uint32_t max_flv_clients = 8;
};

struct FrameServiceDependencies {
  MediaService *media_service = nullptr;
};

struct FrameHlsEntry {
  uint64_t sequence = 0;
  int64_t duration_us = 0;
};

struct FrameHlsPlaylist {
  bool supported = false;
  uint32_t target_duration_sec = 0;
  uint64_t media_sequence = 0;
  std::vector<FrameHlsEntry> entries;
};

struct FrameSegment {
  bool found = false;
  uint64_t sequence = 0;
  int64_t duration_us = 0;
  std::string body;
};

struct FrameFlvBootstrap {
  bool supported = false;
  uint64_t config_generation = 0;
  std::string file_header;
  std::string sequence_header;
  std::string last_keyframe;
};

struct FrameServiceStats {
  bool enabled = false;
  uint64_t hls_segments_created = 0;
  uint32_t active_flv_clients = 0;
};

class IFrameFlvSink {
public:
  virtual ~IFrameFlvSink() = default;

  virtual bool OnFlvChunk(const uint8_t *data, size_t size) = 0;
};

class IFrameService {
public:
  virtual ~IFrameService() = default;

  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual bool IsHlsSupported(StreamId stream_id) const = 0;
  virtual bool IsFlvSupported(StreamId stream_id) const = 0;
  virtual FrameHlsPlaylist GetHlsPlaylist(StreamId stream_id) const = 0;
  virtual FrameSegment GetHlsSegment(StreamId stream_id,
                                     uint64_t sequence) const = 0;
  virtual FrameFlvBootstrap GetFlvBootstrap(StreamId stream_id) const = 0;
  virtual FrameFlvClientId
  AttachFlvClient(StreamId stream_id, uint64_t config_generation,
                  const std::shared_ptr<IFrameFlvSink> &sink) = 0;
  virtual bool DetachFlvClient(FrameFlvClientId client_id) = 0;
  virtual FrameServiceStats GetStats() const = 0;
};

std::unique_ptr<IFrameService>
CreateFrameService(const FrameServiceOptions &options,
                   const FrameServiceDependencies &dependencies);

} // namespace live_stream

#endif // LIVE_STREAM_FRAME_SERVICE_H_
