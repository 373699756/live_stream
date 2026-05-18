#include "rtsp_service.h"

#include "media/frame_source.h"
#include "media_service.h"
#include "net_service.h"

class FakeMediaService : public live_stream::MediaService {
public:
    infra::Result<live_stream::FrameSubscriptionId> SubscribeFrames(
        const live_stream::FrameSubscribeOptions& options,
        live_stream::IFrameSink* sink) override {
        if (sink == nullptr) {
            return infra::Result<live_stream::FrameSubscriptionId>::Fail(
                infra::Status::kInvalidParam);
        }
        if (options.stream_id == StreamId::kMain) {
            main_sink = sink;
            return infra::Result<live_stream::FrameSubscriptionId>::Ok(1);
        }
        if (options.stream_id == StreamId::kSub) {
            sub_sink = sink;
            return infra::Result<live_stream::FrameSubscriptionId>::Ok(2);
        }
        return infra::Result<live_stream::FrameSubscriptionId>::Fail(
            infra::Status::kInvalidParam);
    }

    infra::Status UnsubscribeFrames(
        live_stream::FrameSubscriptionId subscription_id) override {
        if (subscription_id == 1) {
            main_sink = nullptr;
            return infra::Status::kOk;
        }
        if (subscription_id == 2) {
            sub_sink = nullptr;
            return infra::Status::kOk;
        }
        return infra::Status::kNotFound;
    }

    infra::Status RequestKeyFrame(StreamId stream_id,
                                  live_stream::KeyFrameReason reason) override {
        last_key_frame_stream = stream_id;
        last_key_frame_reason = reason;
        ++key_frame_requests;
        return infra::Status::kOk;
    }

    live_stream::IFrameSink* main_sink = nullptr;
    live_stream::IFrameSink* sub_sink = nullptr;
    StreamId last_key_frame_stream = StreamId::kSnapshot;
    live_stream::KeyFrameReason last_key_frame_reason =
        live_stream::KeyFrameReason::kRecovery;
    int key_frame_requests = 0;
};

int main() {
    auto netframe = live_stream::CreateNetEngine(live_stream::NetEngineOptions{});
    if (!netframe.IsOk()) {
        return 1;
    }

    FakeMediaService media;
    live_stream::RtspServiceOptions options;
    options.listen_ip = "127.0.0.1";
    options.listen_port = 0;

    live_stream::RtspServiceDependencies deps;
    deps.net_engine = netframe.value.get();
    deps.media_service = &media;

    auto rtsp = live_stream::CreateRtspService(options, deps);
    if (!rtsp || rtsp->Start() != infra::Status::kOk) {
        return 2;
    }
    if (media.main_sink == nullptr || media.sub_sink == nullptr) {
        return 3;
    }
    rtsp->Stop();
    if (media.main_sink != nullptr || media.sub_sink != nullptr) {
        return 4;
    }
    rtsp->Deinit();
    netframe.value->Stop();
    return 0;
}
