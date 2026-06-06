#include "media_subsystem.h"

#include "core_subsystem.h"
#include "device_subsystem.h"
#include "infra/log.h"
#include "hisi_vendor/mpp_hisi_sdk.h"

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

    DeviceMediaOptions media_options;
    media_options.config = config;
    media_options.sdk = &sdk;
    media_ = CreateDeviceMedia(media_options);
    if (!media_ || !media_->Start()) {
        Error("app", "Start device_media failed");
        Stop();
        return false;
    }
    const MediaChannels media_channels = media_->GetChannels();

    SnapshotOptions snapshot_options;
    snapshot_options.config = config;
    snapshot_options.device_media = media_.get();
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
    ai_options.device_media = media_.get();
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

    RegionOptions overlay_options;
    overlay_options.config = config;
    overlay_options.device_media = media_.get();
    overlay_options.media_channels = media_channels;
    overlay_options.sdk = &sdk;
    overlay_.reset(new Region(overlay_options));
    if (!overlay_ || !overlay_->Start()) {
        Error("app", "Start region failed");
        Stop();
        return false;
    }

    started_ = true;
    return true;
}

void MediaSubsystem::Stop() {
    if (ai_) {
        ai_->Stop();
        ai_.reset();
    }
    if (snapshot_) {
        snapshot_->Stop();
        snapshot_.reset();
    }
    if (overlay_) {
        overlay_->Stop();
        overlay_.reset();
    }
    if (media_) {
        media_->Stop();
        media_.reset();
    }
    started_ = false;
}

MediaRefs MediaSubsystem::refs() const {
    MediaRefs refs;
    refs.media = media_.get();
    refs.ai = ai_.get();
    refs.overlay = overlay_.get();
    refs.snapshot = snapshot_.get();
    return refs;
}

}  // namespace live_stream
