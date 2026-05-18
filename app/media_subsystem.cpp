#include "media_subsystem.h"

#include "core_services.h"
#include "infra/log.h"
#include "hisi_vendor/mpp_hisi_sdk.h"
#include "live_stream/json_utils.h"

namespace live_stream {
namespace {

bool LoadAiEnabled(IConfigService *config, bool *enabled) {
    if (config == nullptr || enabled == nullptr) {
        return false;
    }
    ConfigJson ai_config = config->GetValue("ai");
    return ai_config.is_object() &&
           json_utils::Load(ai_config, "enabled", enabled);
}

}  // namespace

MediaSubsystem &MediaSubsystem::Get() {
    static MediaSubsystem subsystem;
    return subsystem;
}

bool MediaSubsystem::Start() {
    if (started_) {
        return true;
    }

    IConfigService *config = CoreServices::Get().config();
    bool ai_enabled = false;
    if (!LoadAiEnabled(config, &ai_enabled)) {
        INFRA_LOG_ERROR("app", "Load ai config failed");
        Stop();
        return false;
    }

    hisisdk::IHisiSdk &sdk = hisisdk::MppSdk();

    MediaServiceOptions media_options;
    media_options.config_service = config;
    media_options.sdk = &sdk;
    media_.reset(new MediaService(media_options));
    if (!media_ || !media_->Start()) {
        INFRA_LOG_ERROR("app", "Start media service failed");
        Stop();
        return false;
    }

    if (ai_enabled) {
        AiServiceOptions ai_options;
        ai_options.config_service = config;
        ai_options.media_service = media_.get();
        ai_.reset(new AiService(ai_options));
        if (!ai_ || !ai_->Start()) {
            INFRA_LOG_ERROR("app", "Start ai service failed");
            Stop();
            return false;
        }
        INFRA_LOG_INFO("app", "AI service enabled");
    } else {
        INFRA_LOG_INFO("app", "AI service disabled");
    }

    OsdServiceOptions osd_options;
    osd_options.config_service = config;
    osd_options.media_service = media_.get();
    osd_options.sdk = &sdk;
    osd_.reset(new OsdService(osd_options));
    if (!osd_ || !osd_->Start()) {
        INFRA_LOG_ERROR("app", "Start osd service failed");
        Stop();
        return false;
    }

    SnapshotServiceOptions snapshot_options;
    snapshot_options.config_service = config;
    snapshot_options.media_service = media_.get();
    snapshot_options.sdk = &sdk;
    snapshot_.reset(new SnapshotService(snapshot_options));
    if (!snapshot_ || !snapshot_->Start()) {
        INFRA_LOG_ERROR("app", "Start snapshot service failed");
        Stop();
        return false;
    }

    started_ = true;
    return true;
}

void MediaSubsystem::Stop() {
    if (snapshot_) {
        snapshot_->Stop();
        snapshot_.reset();
    }
    if (ai_) {
        ai_->Stop();
        ai_.reset();
    }
    if (osd_) {
        osd_->Stop();
        osd_.reset();
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
    refs.osd = osd_.get();
    refs.snapshot = snapshot_.get();
    return refs;
}

}  // namespace live_stream
