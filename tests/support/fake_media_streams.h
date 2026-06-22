#ifndef LIVE_STREAM_TESTS_SUPPORT_FAKE_MEDIA_STREAMS_H_
#define LIVE_STREAM_TESTS_SUPPORT_FAKE_MEDIA_STREAMS_H_

#include "media/media_streams.h"

namespace live_stream {
namespace test {

class FakeMediaStreams : public MediaStreams {
public:
    FakeMediaStreams()
        : MediaStreams(MakeOptions(this)) {
        Start();
        SetStreamState(StreamId::kMain, MediaStreamState::kRunning,
                       Codec::kH264);
        SetStreamState(StreamId::kSub, MediaStreamState::kRunning,
                       Codec::kH264);
    }

    ~FakeMediaStreams() { Stop(); }

    bool DeliverFrame(const EncodedFrame &encoded_frame) {
        return PushFrame(encoded_frame);
    }

    uint32_t ActiveSubscriptionCount() const {
        return GetStreamStats().active_subscriptions;
    }

    StreamId last_keyframe_stream = StreamId::kMain;
    KeyframeRequestSource last_keyframe_source =
        KeyframeRequestSource::kRecovery;
    int request_keyframes = 0;

private:
    static MediaStreamsOptions MakeOptions(FakeMediaStreams *self) {
        MediaStreamsOptions options;
        options.max_frame_subscriptions = 16;
        options.request_keyframe = &FakeMediaStreams::RequestKeyframe;
        options.request_keyframe_user = self;
        return options;
    }

    static bool RequestKeyframe(StreamId stream_id,
                                KeyframeRequestSource source,
                                void *user) {
        FakeMediaStreams *self = static_cast<FakeMediaStreams *>(user);
        if (self == nullptr) {
            return false;
        }
        self->last_keyframe_stream = stream_id;
        self->last_keyframe_source = source;
        ++self->request_keyframes;
        return true;
    }
};

}  // namespace test
}  // namespace live_stream

#endif  // LIVE_STREAM_TESTS_SUPPORT_FAKE_MEDIA_STREAMS_H_
