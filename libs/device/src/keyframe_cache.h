#ifndef LIVE_STREAM_DEVICE_SRC_KEYFRAME_CACHE_H_
#define LIVE_STREAM_DEVICE_SRC_KEYFRAME_CACHE_H_

#include "media/encoded_frame.h"

namespace live_stream {
namespace device_internal {

class KeyframeCache {
public:
    ~KeyframeCache();

    void Remember(const EncodedFrame &frame);
    void Clear();
    bool Get(StreamId stream_id, EncodedFrame *frame) const;

private:
    struct CachedKeyframe {
        EncodedFrame frame;
        bool has_frame = false;
        bool has_parameter_sets = false;
    };

    CachedKeyframe *FindMutable(StreamId stream_id);
    const CachedKeyframe *Find(StreamId stream_id) const;

    CachedKeyframe main_;
    CachedKeyframe sub_;
};

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_KEYFRAME_CACHE_H_
