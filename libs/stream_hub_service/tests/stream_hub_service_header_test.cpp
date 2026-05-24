#include "stream_hub_service.h"

#include "media/media_buffer.h"

#include <memory>
#include <string>

namespace {

class FakeMediaService : public live_stream::IMediaService {
public:
  bool Start() override { return true; }
  void Stop() override {}
  bool IsStarted() const override { return true; }
  bool IsRestarting() const override { return false; }
  bool IsStreamStarted(live_stream::StreamId stream_id) const override {
    return stream_id == live_stream::StreamId::kMain ||
           stream_id == live_stream::StreamId::kSub;
  }
  live_stream::VideoCodec GetStreamCodec(
      live_stream::StreamId stream_id) const override {
    (void)stream_id;
    return live_stream::VideoCodec::kH264;
  }

  live_stream::FrameAttachId AttachFrameSink(
      const live_stream::FrameAttachOptions& options,
      live_stream::IFrameSink* sink) override {
    if (sink == nullptr) {
      return 0;
    }
    if (options.stream_id == live_stream::StreamId::kMain) {
      main_sink = sink;
      return 1;
    }
    if (options.stream_id == live_stream::StreamId::kSub) {
      sub_sink = sink;
      return 2;
    }
    return 0;
  }

  bool DetachFrameSink(live_stream::FrameAttachId attach_id) override {
    if (attach_id == 1) {
      main_sink = nullptr;
      return true;
    }
    if (attach_id == 2) {
      sub_sink = nullptr;
      return true;
    }
    return false;
  }

  bool RequestKeyFrame(live_stream::StreamId stream_id,
                       live_stream::KeyFrameReason reason) override {
    last_key_frame_stream = stream_id;
    last_key_frame_reason = reason;
    ++key_frame_count;
    return true;
  }

  live_stream::MediaCapabilities GetCapabilities() const override {
    return live_stream::MediaCapabilities();
  }

  live_stream::MediaChannels GetChannels() const override {
    return live_stream::MediaChannels();
  }

  live_stream::ImageStrategyStatus GetImageStrategyStatus() const override {
    return live_stream::ImageStrategyStatus();
  }

  live_stream::IFrameSink* main_sink = nullptr;
  live_stream::IFrameSink* sub_sink = nullptr;
  live_stream::StreamId last_key_frame_stream = live_stream::StreamId::kSnapshot;
  live_stream::KeyFrameReason last_key_frame_reason =
      live_stream::KeyFrameReason::kRecovery;
  int key_frame_count = 0;
};

class CountingFrameSink : public live_stream::IFrameSink {
public:
  const char* Name() const override { return "counting_sink"; }

  void OnFrame(const live_stream::FramePayload& frame) override {
    ++frame_count;
    last_stream = frame.encoded_frame.stream_id;
  }

  void OnSourceStateChanged(live_stream::StreamId stream_id,
                            live_stream::StreamState state) override {
    (void)stream_id;
    last_state = state;
  }

  int frame_count = 0;
  live_stream::StreamId last_stream = live_stream::StreamId::kSnapshot;
  live_stream::StreamState last_state = live_stream::StreamState::kClosed;
};

}  // namespace

int main() {
  FakeMediaService media_service;
  live_stream::StreamHubServiceDependencies dependencies;
  dependencies.media_service = &media_service;

  live_stream::StreamHubServiceOptions options;
  std::unique_ptr<live_stream::IStreamHubService> service =
      live_stream::CreateStreamHubService(options, dependencies);
  if (!service || !service->Start()) {
    return 1;
  }
  if (media_service.main_sink == nullptr || media_service.sub_sink == nullptr ||
      media_service.key_frame_count != 2) {
    return 2;
  }

  media_service.main_sink->OnSourceStateChanged(
      live_stream::StreamId::kMain, live_stream::StreamState::kRunning);
  live_stream::IStreamBrowserSource* browser_source = service.get();
  if (!browser_source->IsStreamAvailable(live_stream::StreamId::kMain) ||
      !browser_source->IsHlsSupported(live_stream::StreamId::kMain) ||
      !browser_source->IsFlvSupported(live_stream::StreamId::kMain)) {
    return 3;
  }

  CountingFrameSink frame_sink;
  live_stream::FrameAttachOptions attach_options;
  attach_options.stream_id = live_stream::StreamId::kMain;
  attach_options.require_key_frame_first = true;
  attach_options.sink_name = "unit_test";
  live_stream::FrameAttachId attach_id =
      service->AttachFrameSink(attach_options, &frame_sink);
  if (attach_id == 0 || media_service.key_frame_count != 3) {
    return 4;
  }
  if (!service->DetachFrameSink(attach_id)) {
    return 5;
  }

  live_stream::StreamHubServiceStats stats = service->GetStats();
  if (!stats.enabled || stats.active_frame_sinks != 0) {
    return 6;
  }
  service->Stop();
  return service->GetStats().enabled ? 7 : 0;
}
