#include "snapshot_service.h"

#include <cstring>

int main() {
    live_stream::MediaChannels channels;
    channels.vpss = live_stream::MppChannel{live_stream::MppModule::kVpss, 0, 0};
    channels.venc = live_stream::MppChannel{live_stream::MppModule::kVenc, 0, 0};
    channels.video_pipe = 0;
    channels.snap_pipe = 2;

    live_stream::SnapshotService snapshot;
    if (std::strcmp(live_stream::SnapshotService::StaticName(), "snapshot_service") != 0) {
        return 3;
    }
    if (std::strcmp(snapshot.Name(), "snapshot_service") != 0) {
        return 4;
    }
    if (snapshot.Init() != infra::Status::kOk) {
        return 5;
    }
    if (snapshot.Start() != infra::Status::kBusy) {
        return 6;
    }
    if (snapshot.BindMedia(channels) != infra::Status::kOk) {
        return 7;
    }
    if (snapshot.Start() != infra::Status::kOk) {
        return 8;
    }

    live_stream::CaptureRequest request;
    const infra::Result<live_stream::SnapshotFrame> frame = snapshot.Capture(request);
    if (!frame.IsOk()) {
        return 9;
    }
    if (frame.value.width != 1920 || frame.value.height != 1080) {
        return 10;
    }
    if (!frame.value.buffer || frame.value.size == 0 ||
        !infra::IsValidBufferSlice(frame.value.PayloadSlice())) {
        return 12;
    }
    if (snapshot.IsCapturing()) {
        return 11;
    }
    snapshot.Stop();
    snapshot.Deinit();
    return 0;
}
