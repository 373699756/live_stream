#ifndef LIVE_STREAM_TESTS_SUPPORT_FAKE_MEDIA_SOURCE_H_
#define LIVE_STREAM_TESTS_SUPPORT_FAKE_MEDIA_SOURCE_H_

#include "media_source.h"

#include <map>
#include <vector>

namespace live_stream {
namespace test {

class FakeMediaFrameSource : public IMediaFrameSource {
public:
    ~FakeMediaFrameSource() override {
        ClearAllSubscriptions();
    }

    bool IsStreamAvailable(StreamId stream_id) const override {
        return stream_id == StreamId::kMain || stream_id == StreamId::kSub;
    }

    VideoCodec GetStreamCodec(StreamId stream_id) const override {
        (void)stream_id;
        return VideoCodec::kH264;
    }

    FrameSubscriptionId SubscribeFrames(
        const SubscriptionOptions &options) override {
        if (!IsStreamAvailable(options.stream_id)) {
            return 0;
        }
        const FrameSubscriptionId subscription_id = next_subscription_id_++;
        Subscription subscription;
        subscription.stream_id = options.stream_id;
        subscription.generation = next_generation_++;
        subscriptions_[subscription_id] = subscription;
        if (options.stream_id == StreamId::kMain) {
            main_subscription_id = subscription_id;
        } else {
            sub_subscription_id = subscription_id;
        }
        ++attached_subscriptions;
        return subscription_id;
    }

    bool UnsubscribeFrames(FrameSubscriptionId subscription_id,
                           SubscriptionClose reason) override {
        (void)reason;
        auto iter = subscriptions_.find(subscription_id);
        if (iter == subscriptions_.end()) {
            return false;
        }
        ClearFrames(&iter->second.pending_frames);
        subscriptions_.erase(iter);
        if (subscription_id == main_subscription_id) {
            main_subscription_id = 0;
        }
        if (subscription_id == sub_subscription_id) {
            sub_subscription_id = 0;
        }
        ++detached_subscriptions;
        return true;
    }

    SubscriptionInfo GetSubscriptionInfo(
        FrameSubscriptionId subscription_id) const override {
        SubscriptionInfo status;
        auto iter = subscriptions_.find(subscription_id);
        if (iter == subscriptions_.end()) {
            status.close_reason = SubscriptionClose::kDetached;
            return status;
        }
        status.attached = true;
        status.stream_id = iter->second.stream_id;
        status.subscription_generation = iter->second.generation;
        status.pending_frames =
            static_cast<uint32_t>(iter->second.pending_frames.size());
        return status;
    }

    SubscriptionStart GetSubscriptionStart(
        FrameSubscriptionId subscription_id) const override {
        SubscriptionStart start_data;
        auto iter = subscriptions_.find(subscription_id);
        if (iter == subscriptions_.end()) {
            return start_data;
        }
        start_data.stream_running = true;
        start_data.gop_complete = true;
        start_data.subscription_generation = iter->second.generation;
        start_data.track = TrackForStream(iter->second.stream_id);
        return start_data;
    }

    bool PopSubscriptionFrame(FrameSubscriptionId subscription_id,
                              SubscriptionFrame *frame) override {
        if (frame == nullptr) {
            return false;
        }
        auto iter = subscriptions_.find(subscription_id);
        if (iter == subscriptions_.end() || iter->second.pending_frames.empty()) {
            return false;
        }
        MediaFrame next_frame;
        if (!MediaFrameMove(&next_frame, &iter->second.pending_frames.front())) {
            return false;
        }
        iter->second.pending_frames.erase(iter->second.pending_frames.begin());
        SubscriptionFrameUnref(frame);
        frame->subscription_id = subscription_id;
        frame->subscription_generation = iter->second.generation;
        frame->starts_on_keyframe = next_frame.keyframe;
        return MediaFrameMove(&frame->frame, &next_frame);
    }

    bool RequestKeyframe(StreamId stream_id,
                         KeyframeRequestSource source) override {
        last_keyframe_stream = stream_id;
        last_keyframe_source = source;
        ++request_keyframes;
        return true;
    }

    bool DeliverFrame(const EncodedFrame &encoded_frame) {
        bool delivered = false;
        for (auto &item : subscriptions_) {
            Subscription &subscription = item.second;
            if (subscription.stream_id != encoded_frame.stream_id) {
                continue;
            }
            MediaFrame frame;
            const bool keyframe = encoded_frame.frame_type == FrameType::kIdr ||
                                  encoded_frame.frame_type == FrameType::kI;
            if (!MediaFrameSetEncodedFrame(&frame, &encoded_frame,
                                           MediaTrackType::kVideo, keyframe,
                                           40000)) {
                return false;
            }
            subscription.pending_frames.push_back(MediaFrame{});
            if (!MediaFrameMove(&subscription.pending_frames.back(), &frame)) {
                subscription.pending_frames.pop_back();
                return false;
            }
            delivered = true;
        }
        return delivered;
    }

    uint32_t ActiveSubscriptionCount() const {
        return static_cast<uint32_t>(subscriptions_.size());
    }

    FrameSubscriptionId main_subscription_id = 0;
    FrameSubscriptionId sub_subscription_id = 0;
    int attached_subscriptions = 0;
    int detached_subscriptions = 0;
    StreamId last_keyframe_stream = StreamId::kMain;
    KeyframeRequestSource last_keyframe_source = KeyframeRequestSource::kRecovery;
    int request_keyframes = 0;

private:
    struct Subscription {
        StreamId stream_id = StreamId::kMain;
        uint64_t generation = 0;
        std::vector<MediaFrame> pending_frames;
    };

    static MediaTrack TrackForStream(StreamId stream_id) {
        MediaTrack track;
        track.stream_id = stream_id;
        track.codec = VideoCodec::kH264;
        track.clock_rate = 90000;
        track.codec_generation = 1;
        track.ready = true;
        track.sps.assign(1, '\x67');
        track.pps.assign(1, '\x68');
        return track;
    }

    void ClearAllSubscriptions() {
        for (auto &item : subscriptions_) {
            ClearFrames(&item.second.pending_frames);
        }
        subscriptions_.clear();
        main_subscription_id = 0;
        sub_subscription_id = 0;
    }

    static void ClearFrames(std::vector<MediaFrame> *frames) {
        if (frames == nullptr) {
            return;
        }
        for (MediaFrame &frame : *frames) {
            MediaFrameUnref(&frame);
        }
        frames->clear();
    }

    FrameSubscriptionId next_subscription_id_ = 1;
    uint64_t next_generation_ = 1;
    std::map<FrameSubscriptionId, Subscription> subscriptions_;
};

}  // namespace test
}  // namespace live_stream

#endif  // LIVE_STREAM_TESTS_SUPPORT_FAKE_MEDIA_SOURCE_H_
