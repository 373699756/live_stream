#ifndef LIVE_STREAM_MEDIA_SOURCE_SRC_GOP_CACHE_H_
#define LIVE_STREAM_MEDIA_SOURCE_SRC_GOP_CACHE_H_

#include "flv_muxer.h"
#include "media_source.h"

#include <array>
#include <cstddef>

namespace live_stream {
namespace media_source_internal {

class GopCache {
public:
    GopCache() = default;
    GopCache(const GopCache &) = delete;
    GopCache &operator=(const GopCache &) = delete;
    ~GopCache();

    void Clear();
    bool complete() const { return complete_; }
    size_t size() const { return size_; }
    uint32_t FirstFlvTagSize() const;
    bool AppendFlvTag(const EncodedFrame &frame, bool keyframe,
                      const FlvVideoTagView &flv_tag_view);
    void CopyTo(MediaFlvStartData *start_data) const;

private:
    bool CopyFlvTagView(const EncodedFrame &frame,
                        const FlvVideoTagView &source,
                        MediaFlvCachedVideoTag *target) const;

    std::array<MediaFlvCachedVideoTag, kMaxMediaFlvCachedVideoTags> frames_;
    size_t head_ = 0;
    size_t size_ = 0;
    bool complete_ = false;
};

}  // namespace media_source_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SOURCE_SRC_GOP_CACHE_H_
