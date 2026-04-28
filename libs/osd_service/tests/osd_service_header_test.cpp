#include "osd_service.h"

#include <cstring>

int main() {
    live_stream::MediaChannels channels;
    channels.vpss = live_stream::MppChannel{live_stream::MppModule::kVpss, 0, 0};
    channels.venc = live_stream::MppChannel{live_stream::MppModule::kVenc, 0, 0};
    channels.video_pipe = 0;
    channels.snap_pipe = 2;

    live_stream::OsdService osd;
    if (std::strcmp(live_stream::OsdService::StaticName(), "osd_service") != 0) {
        return 3;
    }
    if (std::strcmp(osd.Name(), "osd_service") != 0) {
        return 4;
    }
    if (osd.Init() != infra::Status::kOk) {
        return 5;
    }
    if (osd.Start() != infra::Status::kBusy) {
        return 6;
    }
    if (osd.BindMedia(channels) != infra::Status::kOk) {
        return 7;
    }
    if (osd.Start() != infra::Status::kOk) {
        return 8;
    }

    live_stream::OsdRegionConfig config;
    config.target = channels.venc;
    const infra::Result<live_stream::OsdRegionId> id = osd.CreateRegion(config);
    if (!id.IsOk()) {
        return 9;
    }
    if (osd.Attach(id.value) != infra::Status::kOk) {
        return 10;
    }
    if (osd.SetVisible(id.value, false) != infra::Status::kOk) {
        return 11;
    }
    if (osd.RegionCount() != 1) {
        return 12;
    }
    osd.Stop();
    if (osd.DestroyRegion(id.value) != infra::Status::kOk) {
        return 13;
    }
    osd.Deinit();
    return 0;
}
