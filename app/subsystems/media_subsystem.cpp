#include "subsystems/media_subsystem.h"

#include "infra/log.h"
#include "hisi_vendor/mpp_hisi_sdk.h"
#include "subsystems/core_subsystem.h"
#include "subsystems/device_subsystem.h"

namespace live_stream {
namespace {

bool RequestDeviceKeyFrame(StreamId stream_id,
                           KeyFrameRequestType request_type,
                           void *user) {
    DeviceMedia *device = static_cast<DeviceMedia *>(user);
    return device != nullptr &&
           device->RequestKeyFrame(stream_id, request_type);
}

MediaStreamState StreamStateForDeviceStream(DeviceMedia *device,
                                            StreamId stream_id) {
    if (device == nullptr ||
        !device->IsStreamStarted(stream_id)) {
        return MediaStreamState::kClosed;
    }
    return MediaStreamState::kRunning;
}

void SetInitialMediaStreamState(DeviceMedia *device,
                                MediaStreams *media_streams,
                                StreamId stream_id) {
    if (device == nullptr || media_streams == nullptr) {
        return;
    }
    media_streams->SetStreamState(stream_id,
                                  StreamStateForDeviceStream(device,
                                                             stream_id),
                                  device->GetStreamCodec(stream_id));
}

}  // namespace

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

    DeviceMediaOptions device_options;
    device_options.config = config;
    device_options.sdk = &sdk;
    device_ = CreateDeviceMedia(device_options);
    if (!device_) {
        Error("app", "Create device failed");
        Stop();
        return false;
    }

    MediaStreamsOptions media_streams_options;
    media_streams_options.key_frame_request = RequestDeviceKeyFrame;
    media_streams_options.key_frame_request_user = device_.get();
    media_streams_.reset(new MediaStreams(media_streams_options));
    if (!media_streams_ || !media_streams_->Start() ||
        !device_->SetFrameSink(media_streams_.get())) {
        Error("app", "Start media streams failed");
        Stop();
        return false;
    }

    if (!device_->Start()) {
        Error("app", "Start device failed");
        Stop();
        return false;
    }

    SetInitialMediaStreamState(device_.get(), media_streams_.get(),
                               StreamId::kMain);
    SetInitialMediaStreamState(device_.get(), media_streams_.get(),
                               StreamId::kSub);
    const MediaChannels media_channels = device_->GetChannels();

    AiOptions ai_options;
    ai_options.config = config;
    ai_options.alarm = device_refs.alarm;
    ai_options.device = device_.get();
    ai_options.media_channels = media_channels;
    ai_options.sdk = &sdk;
    ai_.reset(new Ai(ai_options));
    if (!ai_ || !ai_->Start()) {
        Error("app", "Start ai failed");
        Stop();
        return false;
    }
    Info("app", "AI ready");

    started_ = true;
    return true;
}

void MediaSubsystem::Stop() {
    if (ai_) {
        ai_->Stop();
        ai_.reset();
    }
    if (device_) {
        (void)device_->SetFrameSink(nullptr);
    }
    if (media_streams_) {
        media_streams_->SetStreamState(StreamId::kMain,
                                       MediaStreamState::kClosed,
                                       Codec::kH264);
        media_streams_->SetStreamState(StreamId::kSub,
                                       MediaStreamState::kClosed,
                                       Codec::kH264);
        media_streams_->Stop();
        media_streams_.reset();
    }
    if (device_) {
        device_->Stop();
        device_.reset();
    }
    started_ = false;
}

MediaRefs MediaSubsystem::refs() const {
    MediaRefs refs;
    refs.device = device_.get();
    refs.media_streams = media_streams_.get();
    refs.ai = ai_.get();
    return refs;
}

}  // namespace live_stream
