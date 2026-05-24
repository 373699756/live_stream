#include "media_subsystem.h"

#include "core_services.h"
#include "infra/log.h"
#include "hisi_vendor/mpp_hisi_sdk.h"

namespace live_stream {

MediaSubsystem &MediaSubsystem::Get() {
    static MediaSubsystem subsystem;
    return subsystem;
}

bool MediaSubsystem::Start(CoreServices &core_services) {
    if (started_) {
        return true;
    }

    IConfigService *config = core_services.config();
    hisisdk::IHisiSdk &sdk = hisisdk::MppSdk();

    MediaServiceOptions media_options;
    media_options.config_service = config;
    media_options.sdk = &sdk;
    media_ = CreateMediaService(media_options);
    if (!media_ || !media_->Start()) {
        INFRA_LOG_ERROR("app", "Start media service failed");
        Stop();
        return false;
    }
    const MediaChannels media_channels = media_->GetChannels();

    SnapshotServiceOptions snapshot_options;
    snapshot_options.config_service = config;
    snapshot_options.media_service = media_.get();
    snapshot_options.media_channels = media_channels;
    snapshot_options.sdk = &sdk;
    snapshot_.reset(new SnapshotService(snapshot_options));
    if (!snapshot_ || !snapshot_->Start()) {
        INFRA_LOG_ERROR("app", "Start snapshot service failed");
        Stop();
        return false;
    }

    AiServiceOptions ai_options;
    ai_options.config_service = config;
    ai_options.media_service = media_.get();
    ai_options.snapshot_service = snapshot_.get();
    ai_options.media_channels = media_channels;
    ai_options.sdk = &sdk;
    ai_.reset(new AiService(ai_options));
    if (!ai_ || !ai_->Start()) {
        INFRA_LOG_ERROR("app", "Start ai service failed");
        Stop();
        return false;
    }
    INFRA_LOG_INFO("app", "AI service ready");

    RegionServiceOptions overlay_options;
    overlay_options.config_service = config;
    overlay_options.media_service = media_.get();
    overlay_options.media_channels = media_channels;
    overlay_options.sdk = &sdk;
    overlay_.reset(new RegionService(overlay_options));
    if (!overlay_ || !overlay_->Start()) {
        INFRA_LOG_ERROR("app", "Start overlay service failed");
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
