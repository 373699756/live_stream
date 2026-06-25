#ifndef LIVE_STREAM_DEVICE_SRC_SNAPSHOT_H_
#define LIVE_STREAM_DEVICE_SRC_SNAPSHOT_H_

#include "device.h"

#include <memory>

namespace live_stream {

class IConfig;

namespace hisisdk {
class IHisiSnapshot;
}  // namespace hisisdk

namespace device_internal {

struct SnapshotOptions {
    SnapshotConfig default_config;
    IConfig *config = nullptr;
    MediaChannels media_channels;
    hisisdk::IHisiSnapshot *snapshot = nullptr;
};

class Snapshot {
public:
    explicit Snapshot(const SnapshotOptions &options);
    ~Snapshot();

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

#endif  // LIVE_STREAM_DEVICE_SRC_SNAPSHOT_H_
