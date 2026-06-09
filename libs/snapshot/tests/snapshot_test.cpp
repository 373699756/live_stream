#include "snapshot.h"

#include "config.h"
#include "hisisdk/hisi_sdk.h"
#include "media/media_buffer.h"
#include "device_media.h"

#include <cstring>
#include <memory>
#include <string>

namespace {

class FakeConfig : public live_stream::IConfig {
public:
  bool Start() override { return true; }
  void Stop() override {}
  bool IsStarted() const override { return true; }

  bool SetValue(const std::string& name,
                const live_stream::ConfigJson& value) override {
    if (name != "snapshot") {
      return false;
    }
    if (attachment.validate && !attachment.validate(value).ok) {
      return false;
    }
    if (attachment.apply && !attachment.apply(value).ok) {
      return false;
    }
    snapshot = value;
    return true;
  }

  live_stream::ConfigJson GetValue(const std::string& name) override {
    if (name != "snapshot") {
      return live_stream::ConfigJson();
    }
    return snapshot;
  }

  bool SetDefault(const std::string& name) override {
    return name == "snapshot";
  }

  live_stream::ConfigJson GetDefault(const std::string& name) override {
    if (name != "snapshot") {
      return live_stream::ConfigJson();
    }
    return default_snapshot;
  }

  bool RestoreDefaults() override {
    snapshot = default_snapshot;
    return true;
  }

  bool AttachConfig(const std::string& name,
                    const live_stream::ConfigAttachment& next) override {
    if (name != "snapshot") {
      return false;
    }
    attachment = next;
    return true;
  }

  bool DetachConfig(const std::string& name) override {
    if (name != "snapshot") {
      return false;
    }
    attachment = live_stream::ConfigAttachment();
    return true;
  }

  live_stream::ConfigJson default_snapshot = {
      {"enabled", true},
      {"jpeg_quality", 85},
      {"timeout_ms", 2000}};
  live_stream::ConfigJson snapshot = default_snapshot;
  live_stream::ConfigAttachment attachment;
};

class FakeDeviceMedia : public live_stream::IDeviceMedia {
public:
  bool Start() override { return true; }
  void Stop() override {}
  bool IsStarted() const override { return true; }
  bool IsRestarting() const override { return false; }
  bool IsStreamStarted(live_stream::StreamId) const override { return true; }
  live_stream::VideoCodec GetStreamCodec(
      live_stream::StreamId) const override {
    return live_stream::VideoCodec::kH264;
  }
  live_stream::FrameAttachId AttachFrameSink(
      const live_stream::FrameAttachOptions&, live_stream::IFrameSink*) override {
    return 0;
  }
  bool DetachFrameSink(live_stream::FrameAttachId) override { return true; }
  bool RequestKeyFrame(live_stream::StreamId,
                       live_stream::KeyFrameReason) override {
    return true;
  }
  live_stream::MediaCapabilities GetCapabilities() const override {
    return live_stream::MediaCapabilities();
  }
  live_stream::MediaChannels GetChannels() const override {
    live_stream::MediaChannels channels;
    channels.vpss = live_stream::MppChannel{live_stream::MppModule::kVpss, 0, 0};
    channels.venc = live_stream::MppChannel{live_stream::MppModule::kVenc, 0, 0};
    channels.video_pipe = 0;
    channels.snap_pipe = 2;
    channels.main_size = live_stream::VideoSize{1920, 1080};
    return channels;
  }
  live_stream::ImageStrategyStatus GetImageStrategyStatus() const override {
    return live_stream::ImageStrategyStatus();
  }
};

class FakeSdk : public live_stream::hisisdk::IHisiSdk {
public:
  live_stream::MediaCapabilities GetCapabilities() override {
    return live_stream::MediaCapabilities();
  }
  bool InitSystem(const live_stream::MediaPipelineConfig&) override { return true; }
  bool DeinitSystem() override { return true; }
  bool StartVi(const live_stream::MediaPipelineConfig&) override { return true; }
  void StopVi(const live_stream::MediaPipelineConfig&) override {}
  bool StartVpss(const live_stream::MediaPipelineConfig&) override { return true; }
  void StopVpss(const live_stream::MediaPipelineConfig&) override {}
  bool BindViVpss(const live_stream::MediaPipelineConfig&) override { return true; }
  void UnbindViVpss(const live_stream::MediaPipelineConfig&) override {}
  bool StartVenc(const live_stream::MediaPipelineConfig&) override { return true; }
  void StopVenc(const live_stream::MediaPipelineConfig&) override {}
  bool BindVpssVenc(const live_stream::MediaPipelineConfig&) override { return true; }
  void UnbindVpssVenc(const live_stream::MediaPipelineConfig&) override {}
  bool StartVencStream(const live_stream::MediaPipelineConfig&,
                       live_stream::EncodedFrameCallback,
                       void*) override {
    return true;
  }
  void StopVencStream(const live_stream::MediaPipelineConfig&) override {}
  bool RequestIdr(int32_t) override { return true; }
  bool ApplyVencRoi(int32_t,
                    const live_stream::VideoStreamConfig&) override {
    return true;
  }
  bool ApplyImageConfig(const live_stream::MediaPipelineConfig&,
                        const live_stream::ConfigJson&) override {
    return true;
  }
  live_stream::hisisdk::ExposureInfo QueryExposureInfo(
      const live_stream::MediaPipelineConfig&) override {
    return live_stream::hisisdk::ExposureInfo();
  }
  bool CreateRegion(int32_t, const live_stream::hisisdk::RegionConfig&) override {
    return true;
  }
  bool AttachRegion(int32_t, const live_stream::hisisdk::RegionConfig&) override {
    return true;
  }
  bool DetachRegion(int32_t, const live_stream::hisisdk::RegionConfig&) override {
    return true;
  }
  bool SetRegionDisplay(int32_t, const live_stream::hisisdk::RegionConfig&) override {
    return true;
  }
  bool SetRegionBitmap(int32_t, const live_stream::hisisdk::Bitmap&) override {
    return true;
  }
  void DestroyRegion(int32_t) override {}
  live_stream::hisisdk::JpegFrame CaptureJpeg(
      const live_stream::hisisdk::SnapshotConfig&) override {
    live_stream::hisisdk::JpegFrame frame;
    frame.buffer = live_stream::VideoBufferAlloc(8);
    if (frame.buffer != nullptr) {
      frame.buffer->data[0] = 0xff;
      frame.buffer->data[1] = 0xd8;
      live_stream::VideoBufferSetSize(frame.buffer, 8);
      frame.size = 8;
      frame.width = 1920;
      frame.height = 1080;
      frame.pts_us = 1234;
    }
    return frame;
  }
  live_stream::hisisdk::YuvFrame CaptureYuvFrame(
      const live_stream::MppChannel&,
      live_stream::hisisdk::Size size,
      uint32_t) override {
    live_stream::hisisdk::YuvFrame frame;
    frame.buffer = live_stream::VideoBufferAlloc(size.width * size.height * 3 / 2);
    if (frame.buffer != nullptr) {
      live_stream::VideoBufferSetSize(frame.buffer, size.width * size.height * 3 / 2);
      frame.size = size.width * size.height * 3 / 2;
      frame.width = size.width;
      frame.height = size.height;
      frame.pts_us = 5678;
    }
    return frame;
  }
};

}  // namespace

int main() {
  if (std::strcmp(live_stream::Snapshot::StaticName(),
                  "snapshot") != 0) {
    return 1;
  }

  live_stream::MediaChannels channels;
  channels.vpss = live_stream::MppChannel{live_stream::MppModule::kVpss, 0, 0};
  channels.sub_vpss =
      live_stream::MppChannel{live_stream::MppModule::kVpss, 0, 1};
  channels.venc = live_stream::MppChannel{live_stream::MppModule::kVenc, 0, 0};
  channels.sub_venc =
      live_stream::MppChannel{live_stream::MppModule::kVenc, 0, 1};
  channels.video_pipe = 0;
  channels.snap_pipe = 2;
  channels.main_size = live_stream::VideoSize{1920, 1080};
  channels.sub_size = live_stream::VideoSize{640, 360};

  live_stream::Snapshot snapshot;
  if (!snapshot.BindMedia(channels)) {
    return 2;
  }
  if (!snapshot.Start()) {
    return 3;
  }
  live_stream::CaptureRequest request;
  live_stream::SnapshotFrame frame = snapshot.Capture(request);
  if (!frame.buffer || frame.width != 1920 || frame.height != 1080 ||
      !frame.HasValidPayload() ||
      !live_stream::IsValidBufferSlice(frame.PayloadSlice())) {
    return 4;
  }
  snapshot.Stop();

  FakeConfig config;
  FakeSdk sdk;
  live_stream::SnapshotOptions options;
  options.config = &config;
  options.media_channels = channels;
  options.sdk = &sdk;
  live_stream::Snapshot configured(options);
  if (!configured.BindMedia(channels) || !configured.Start()) {
    return 5;
  }
  live_stream::SnapshotStats stats = configured.GetStats();
  if (!stats.enabled || stats.jpeg_quality != 85U || stats.timeout_ms != 2000U) {
    return 6;
  }
  config.snapshot["enabled"] = false;
  if (!config.SetValue("snapshot", config.snapshot)) {
    return 7;
  }
  if (!configured.Capture(request).buffer) {
    return 8;
  }
  configured.Stop();
  return 0;
}
