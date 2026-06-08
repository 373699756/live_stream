#include "subsystems/media_subsystem.h"

#include "infra/log.h"
#include "hisi_vendor/mpp_hisi_sdk.h"
#include "subsystems/core_subsystem.h"
#include "subsystems/device_subsystem.h"

namespace live_stream {

MediaSubsystem &MediaSubsystem::Get() {
    static MediaSubsystem subsystem;
    return subsystem;
}

bool MediaSubsystem::Start(CoreSubsystem &core_subsystem,
                           const DeviceRefs &device_refs) {
    if (started_) {
        return true;
    }

    IConfig *config = core_subsystem.config();
    hisisdk::IHisiSdk &sdk = hisisdk::MppSdk();

    DeviceMediaOptions device_media_options;
    device_media_options.config = config;
    device_media_options.sdk = &sdk;
    device_media_ = CreateDeviceMedia(device_media_options);
    if (!device_media_ || !device_media_->Start()) {
        Error("app", "Start device_media failed");
        Stop();
        return false;
    }
    const MediaChannels media_channels = device_media_->GetChannels();

    SnapshotOptions snapshot_options;
    snapshot_options.config = config;
    snapshot_options.device_media = device_media_.get();
    snapshot_options.media_channels = media_channels;
    snapshot_options.sdk = &sdk;
    snapshot_.reset(new Snapshot(snapshot_options));
    if (!snapshot_ || !snapshot_->Start()) {
        Error("app", "Start snapshot failed");
        Stop();
        return false;
    }

    AiOptions ai_options;
    ai_options.config = config;
    ai_options.alarm = device_refs.alarm;
    ai_options.device_media = device_media_.get();
    ai_options.snapshot = snapshot_.get();
    ai_options.media_channels = media_channels;
    ai_options.sdk = &sdk;
    ai_.reset(new Ai(ai_options));
    if (!ai_ || !ai_->Start()) {
        Error("app", "Start ai failed");
        Stop();
        return false;
    }
    Info("app", "AI ready");

    RegionOptions region_options;
    region_options.config = config;
    region_options.device_media = device_media_.get();
    region_options.media_channels = media_channels;
    region_options.sdk = &sdk;
    region_.reset(new Region(region_options));
    if (!region_ || !region_->Start()) {
        Error("app", "Start region failed");
        Stop();
        return false;
    }

    started_ = true;
    return true;
}

void MediaSubsystem::Stop() {
    if (region_) {
        region_->Stop();
        region_.reset();
    }
    if (ai_) {
        ai_->Stop();
        ai_.reset();
    }
    if (snapshot_) {
        snapshot_->Stop();
        snapshot_.reset();
    }
    if (device_media_) {
        device_media_->Stop();
        device_media_.reset();
    }
    started_ = false;
}

MediaRefs MediaSubsystem::refs() const {
    MediaRefs refs;
    refs.device_media = device_media_.get();
    refs.ai = ai_.get();
    refs.snapshot = snapshot_.get();
    return refs;
}

}  // namespace live_stream
