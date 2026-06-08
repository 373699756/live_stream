#include "pending_frame_queue.h"

#include "media_codec.h"

namespace live_stream {
namespace media_pipeline_internal {

void PendingFrameQueue::Clear() {
    for (EncodedFrame &frame : frames_) {
        EncodedFrameUnref(&frame);
    }
    head_ = 0;
    size_ = 0;
}

bool PendingFrameQueue::Empty() const { return size_ == 0; }

bool PendingFrameQueue::Full() const { return size_ >= frames_.size(); }

bool PendingFrameQueue::PushBack(const EncodedFrame &frame) {
    if (Full()) {
        return false;
    }
    if (!EncodedFrameRefCopy(&frames_[(head_ + size_) % frames_.size()],
                             &frame)) {
        return false;
    }
    ++size_;
    return true;
}

bool PendingFrameQueue::PopFront() {
    if (Empty()) {
        return false;
    }
    EncodedFrameUnref(&frames_[head_]);
    head_ = (head_ + 1) % frames_.size();
    --size_;
    return true;
}

bool PendingFrameQueue::DropOldestNonKeyFrame() {
    for (size_t i = 0; i < size_; ++i) {
        const size_t index = (head_ + i) % frames_.size();
        if (!media_codec::IsKeyFrame(frames_[index].frame_type)) {
            RemoveAt(i);
            return true;
        }
    }
    return false;
}

bool PendingFrameQueue::TakeFront(EncodedFrame *frame) {
    if (frame == nullptr || Empty()) {
        return false;
    }
    (void)EncodedFrameMove(frame, &frames_[head_]);
    head_ = (head_ + 1) % frames_.size();
    --size_;
    return true;
}

void PendingFrameQueue::RemoveAt(size_t position) {
    if (position >= size_) {
        return;
    }
    for (size_t i = position; i + 1 < size_; ++i) {
        const size_t target = (head_ + i) % frames_.size();
        const size_t source = (head_ + i + 1) % frames_.size();
        (void)EncodedFrameMove(&frames_[target], &frames_[source]);
    }
    const size_t tail = (head_ + size_ - 1) % frames_.size();
    EncodedFrameUnref(&frames_[tail]);
    --size_;
}

}  // namespace media_pipeline_internal
}  // namespace live_stream
