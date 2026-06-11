#include "pending_frame_queue.h"

namespace live_stream {
namespace media_streams_internal {

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
    // worker 线程稍后 drain，因此队列必须持有自己的 FrameBuffer 引用。
    // 这里只 ref copy，不复制编码 payload。
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
        const bool key_frame =
            frames_[index].frame_type == FrameType::kIdr ||
            frames_[index].frame_type == FrameType::kI;
        if (!key_frame) {
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
    // 出队时转移 owner，避免 ref/unref 抖动；调用方负责最终 EncodedFrameUnref。
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

}  // namespace media_streams_internal
}  // namespace live_stream
