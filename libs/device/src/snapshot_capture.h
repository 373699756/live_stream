#ifndef LIVE_STREAM_DEVICE_SRC_SNAPSHOT_CAPTURE_H_
#define LIVE_STREAM_DEVICE_SRC_SNAPSHOT_CAPTURE_H_

#include "device.h"

#include <memory>

namespace live_stream {

class IConfig;

namespace hisisdk {
class IHisiSdk;
}  // namespace hisisdk

namespace device_internal {

struct SnapshotCaptureOptions {
    SnapshotConfig default_config;
    IConfig *config = nullptr;
    MediaChannels media_channels;
    hisisdk::IHisiSdk *sdk = nullptr;
};

class SnapshotCapture {
public:
    SnapshotCapture();
    explicit SnapshotCapture(const SnapshotCaptureOptions &options);
    ~SnapshotCapture();

    bool Start();
    void Stop();
    void Release();
    bool BindMedia(const MediaChannels &channels);
    SnapshotFrame Capture(const SnapshotRequest &request);
    bool IsCapturing() const;
    SnapshotInfo GetInfo() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace device_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_DEVICE_SRC_SNAPSHOT_CAPTURE_H_
