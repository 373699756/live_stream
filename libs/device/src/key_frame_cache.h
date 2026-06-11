#ifndef LIVE_STREAM_DEVICE_SRC_KEY_FRAME_CACHE_H_
#define LIVE_STREAM_DEVICE_SRC_KEY_FRAME_CACHE_H_

#include "media/encoded_frame.h"

namespace live_stream {
namespace device_internal {

class KeyFrameCache {
public:
    ~KeyFrameCache();

    void Remember(const EncodedFrame &frame);
    void Clear();
    bool Get(StreamId stream_id, EncodedFrame *frame) const;

private:
    struct CachedKeyFrame {
        EncodedFrame frame;
        bool has_frame = false;
        bool has_parameter_sets = false;
    };

    CachedKeyFrame *FindMutable(StreamId stream_id);
    const CachedKeyFrame *Find(StreamId stream_id) const;

    CachedKeyFrame main_;
    CachedKeyFrame sub_;
};

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_KEY_FRAME_CACHE_H_
