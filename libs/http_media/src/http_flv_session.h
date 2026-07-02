#ifndef LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_FLV_SESSION_H_
#define LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_FLV_SESSION_H_

#include "http_media_writer.h"
#include "media/media_streams.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace live_stream {

enum class HttpFlvSessionStartStatus {
    kStarted,
    kNoSession,
    kStartBlock,
    kSequenceHeader,
    kCachedGop,
};

const char *HttpFlvSessionStartStatusName(
    HttpFlvSessionStartStatus status);
bool HttpFlvSessionStartNeedsClose(HttpFlvSessionStartStatus status);

class HttpFlvSession : public IMediaFlvSink {
public:
    HttpFlvSession(HttpMediaWriter *writer, ConnectionId connection_id,
                   StreamId stream_id);

    void Close() override;
    HttpFlvSessionStartStatus Start(const MediaFlvStart &flv_start,
                                    size_t &cached_flv_bytes);
    bool OnFlvChunk(const uint8_t *data, size_t size) override;
    bool OnFlvVideoTag(const MediaFlvVideoTagView &tag,
                       const MediaFrame &frame) override;

private:
    bool OnCachedFlvVideoTag(const MediaFlvCachedVideoTag &tag);
    uint32_t RebaseTimestamp(uint32_t timestamp_ms, bool clamp_backward);

    HttpMediaWriter *writer_ = nullptr;
    ConnectionId connection_id_ = 0;
    StreamId stream_id_ = StreamId::kMain;
    std::string start_block_;
    std::string sequence_header_;
    bool timestamp_base_set_ = false;
    uint32_t timestamp_base_ms_ = 0;
    uint32_t last_timestamp_ms_ = 0;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_MEDIA_SRC_HTTP_FLV_SESSION_H_
