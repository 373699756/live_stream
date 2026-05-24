#ifndef LIVE_STREAM_RTSP_SERVICE_TESTS_FAKE_STREAM_HUB_H_
#define LIVE_STREAM_RTSP_SERVICE_TESTS_FAKE_STREAM_HUB_H_

#include "stream_hub_service.h"

namespace live_stream {
namespace test {

class FakeStreamHub : public IStreamHubService {
public:
    bool Start() override { return true; }
    void Stop() override {}

    bool IsHlsSupported(StreamId stream_id) const override {
        (void)stream_id;
        return false;
    }

    bool IsFlvSupported(StreamId stream_id) const override {
        (void)stream_id;
        return false;
    }

    bool IsMjpegSupported(StreamId stream_id) const override {
        (void)stream_id;
        return false;
    }

    bool IsStreamAvailable(StreamId stream_id) const override {
        return stream_id == StreamId::kMain || stream_id == StreamId::kSub;
    }

    VideoCodec GetStreamCodec(StreamId stream_id) const override {
        (void)stream_id;
        return VideoCodec::kH264;
    }

    StreamHlsPlaylist GetHlsPlaylist(StreamId stream_id) const override {
        (void)stream_id;
        return StreamHlsPlaylist{};
    }

    StreamSegmentRef GetHlsSegmentRef(StreamId stream_id,
                                uint64_t sequence) const override {
        (void)stream_id;
        (void)sequence;
        return StreamSegmentRef{};
    }

    StreamFlvStartData GetFlvStartData(StreamId stream_id) const override {
        (void)stream_id;
        return StreamFlvStartData{};
    }

    StreamBrowserStatus GetBrowserStatus(StreamId stream_id) const override {
        (void)stream_id;
        return StreamBrowserStatus{};
    }

    StreamFlvClientId AttachFlvClient(StreamId stream_id,
                                      uint64_t config_generation,
                                      bool wait_for_keyframe,
                                      IStreamFlvSink* sink) override {
        (void)stream_id;
        (void)config_generation;
        (void)wait_for_keyframe;
        (void)sink;
        return 0;
    }

    bool DetachFlvClient(StreamFlvClientId client_id) override {
        (void)client_id;
        return false;
    }

    StreamMjpegClientId AttachMjpegClient(StreamId stream_id,
                                          IStreamMjpegSink* sink) override {
        (void)stream_id;
        (void)sink;
        return 0;
    }

    bool DetachMjpegClient(StreamMjpegClientId client_id) override {
        (void)client_id;
        return false;
    }

    FrameAttachId AttachFrameSink(const FrameAttachOptions& options,
                                        IFrameSink* sink) override {
        if (sink == nullptr) {
            return 0;
        }
        const FrameAttachId id = next_sink_id_++;
        if (options.stream_id == StreamId::kMain) {
            main_sink = sink;
            main_sink_id = id;
        } else if (options.stream_id == StreamId::kSub) {
            sub_sink = sink;
            sub_sink_id = id;
        }
        return id;
    }

    bool DetachFrameSink(FrameAttachId sink_id) override {
        if (sink_id == main_sink_id) {
            main_sink = nullptr;
            main_sink_id = 0;
            return true;
        }
        if (sink_id == sub_sink_id) {
            sub_sink = nullptr;
            sub_sink_id = 0;
            return true;
        }
        return false;
    }

    bool RequestKeyFrame(StreamId stream_id, KeyFrameReason reason) override {
        last_key_frame_stream = stream_id;
        last_key_frame_reason = reason;
        ++key_frame_requests;
        return true;
    }

    StreamHubServiceStats GetStats() const override {
        StreamHubServiceStats stats;
        stats.enabled = true;
        stats.active_frame_sinks =
            static_cast<uint32_t>((main_sink != nullptr ? 1 : 0) +
                                  (sub_sink != nullptr ? 1 : 0));
        return stats;
    }

    bool DeliverFrame(const EncodedFrame& frame) {
        IFrameSink* sink = frame.stream_id == StreamId::kSub ? sub_sink
                                                             : main_sink;
        if (sink == nullptr) {
            return false;
        }
        FramePayload payload;
        if (!EncodedFrameRefCopy(&payload.encoded_frame, &frame)) {
            return false;
        }
        sink->OnFrame(payload);
        FramePayloadUnref(&payload);
        return true;
    }

    IFrameSink* main_sink = nullptr;
    IFrameSink* sub_sink = nullptr;
    FrameAttachId main_sink_id = 0;
    FrameAttachId sub_sink_id = 0;
    StreamId last_key_frame_stream = StreamId::kMain;
    KeyFrameReason last_key_frame_reason = KeyFrameReason::kRecovery;
    int key_frame_requests = 0;

private:
    FrameAttachId next_sink_id_ = 1;
};

}  // namespace test
}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SERVICE_TESTS_FAKE_STREAM_HUB_H_
