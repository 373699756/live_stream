#ifndef LIVE_STREAM_MEDIA_SRC_GOP_CACHE_H_
#define LIVE_STREAM_MEDIA_SRC_GOP_CACHE_H_

#include "flv_muxer.h"
#include "media/media_streams.h"

#include <array>
#include <cstddef>

namespace live_stream {
namespace media_internal {

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
    bool AppendFlvTag(const MediaFrame &frame, bool keyframe,
                      const FlvVideoTagBuild &flv_tag_view);
    void CopyTo(MediaFlvStart *flv_start) const;

private:
    // FLV GOP cache 从最近关键帧开始保存完整 GOP。HTTP-FLV 新客户端连接时
    // 先拿 sequence header，再从这里取得可解码起点。
    bool CopyFlvTagView(const MediaFrame &frame,
                        const FlvVideoTagBuild &source,
                        MediaFlvCachedVideoTag *target) const;

    std::array<MediaFlvCachedVideoTag, kMaxMediaFlvCachedVideoTags> frames_;
    size_t head_ = 0;
    size_t size_ = 0;
    bool complete_ = false;
};

}  // namespace media_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_GOP_CACHE_H_
