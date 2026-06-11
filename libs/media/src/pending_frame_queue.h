#ifndef LIVE_STREAM_MEDIA_SRC_PENDING_FRAME_QUEUE_H_
#define LIVE_STREAM_MEDIA_SRC_PENDING_FRAME_QUEUE_H_

#include "media/encoded_frame.h"

#include <array>
#include <cstddef>

namespace live_stream {
namespace media_streams_internal {

constexpr size_t kMaxPendingFramesPerStream = 4;

class PendingFrameQueue {
public:
    void Clear();
    bool Empty() const;
    bool Full() const;
    bool PushBack(const EncodedFrame &frame);
    bool PopFront();
    bool DropOldestNonKeyframe();
    bool TakeFront(EncodedFrame *frame);

private:
    void RemoveAt(size_t position);

    std::array<EncodedFrame, kMaxPendingFramesPerStream> frames_{};
    size_t head_ = 0;
    size_t size_ = 0;
};

}  // namespace media_streams_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_MEDIA_SRC_PENDING_FRAME_QUEUE_H_
