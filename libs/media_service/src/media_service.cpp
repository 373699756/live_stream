#include "media_service.h"

#include "hisisdk/hisi_sdk.h"
#include "media_pipeline.h"

#include <map>
#include <utility>

namespace live_stream {
namespace {

enum class ServiceState {
    kCreated = 0,
    kInitialized,
    kStarted,
    kStopping,
    kStopped,
    kDeinitialized,
};

}  // namespace

struct MediaService::Impl {
    explicit Impl(MediaPipelineConfig config) : pipeline(std::move(config)) {}

    MediaPipeline pipeline;
    ServiceState state = ServiceState::kCreated;
    EncodedFrameCallback callback = nullptr;
    void* callback_user = nullptr;
    std::map<FrameSubscriptionId, std::pair<FrameSubscribeOptions, IFrameSink*>>
        sinks;
    FrameSubscriptionId next_subscription_id = 1;

    void NotifySourceState(StreamState stream_state) {
        for (const auto& item : sinks) {
            if (item.second.second != nullptr) {
                item.second.second->OnSourceStateChanged(
                    item.second.first.stream_id, stream_state);
            }
        }
    }
};

MediaService::MediaService() : MediaService(MediaPipelineConfig{}) {}

MediaService::MediaService(const MediaPipelineConfig& config)
    : impl_(new Impl(config)) {}

MediaService::~MediaService() {
    Deinit();
    delete impl_;
    impl_ = nullptr;
}

infra::Status MediaService::Init() {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    if (impl_->state == ServiceState::kInitialized ||
        impl_->state == ServiceState::kStarted ||
        impl_->state == ServiceState::kStopped) {
        return infra::Status::kOk;
    }
    if (impl_->state != ServiceState::kCreated &&
        impl_->state != ServiceState::kDeinitialized) {
        return infra::Status::kBusy;
    }

    const infra::Status status = impl_->pipeline.InitSystem();
    if (status != infra::Status::kOk) {
        impl_->pipeline.DeinitSystem();
        return status;
    }

    impl_->state = ServiceState::kInitialized;
    return infra::Status::kOk;
}

infra::Status MediaService::Start() {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    if (impl_->state == ServiceState::kStarted) {
        return infra::Status::kOk;
    }
    if (impl_->state == ServiceState::kStopped) {
        impl_->state = ServiceState::kInitialized;
    }
    if (impl_->state != ServiceState::kInitialized) {
        return infra::Status::kBusy;
    }

    const infra::Status status = impl_->pipeline.Start();
    if (status != infra::Status::kOk) {
        impl_->pipeline.Stop();
        impl_->state = ServiceState::kInitialized;
        return status;
    }

    impl_->state = ServiceState::kStarted;
    impl_->NotifySourceState(StreamState::kRunning);
    return infra::Status::kOk;
}

void MediaService::Stop() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->state != ServiceState::kStarted) {
        if (impl_->state == ServiceState::kStopping) {
            impl_->state = ServiceState::kStopped;
        }
        return;
    }

    impl_->state = ServiceState::kStopping;
    impl_->NotifySourceState(StreamState::kClosed);
    impl_->pipeline.Stop();
    impl_->state = ServiceState::kStopped;
}

void MediaService::Deinit() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->state == ServiceState::kStarted) {
        Stop();
    }
    if (impl_->state == ServiceState::kDeinitialized ||
        impl_->state == ServiceState::kCreated) {
        return;
    }

    impl_->pipeline.DeinitSystem();
    impl_->state = ServiceState::kDeinitialized;
}

const char* MediaService::Name() const {
    return StaticName();
}

const char* MediaService::StaticName() {
    return "media_service";
}

infra::Result<FrameSubscriptionId> MediaService::SubscribeFrames(
    const FrameSubscribeOptions& options,
    IFrameSink* sink) {
    if (impl_ == nullptr) {
        return infra::Result<FrameSubscriptionId>::Fail(
            infra::Status::kInternalError);
    }
    if (sink == nullptr || !IsValidMediaStream(options.stream_id)) {
        return infra::Result<FrameSubscriptionId>::Fail(
            infra::Status::kInvalidParam);
    }
    if (impl_->state != ServiceState::kStarted) {
        return infra::Result<FrameSubscriptionId>::Fail(infra::Status::kBusy);
    }

    const FrameSubscriptionId id = impl_->next_subscription_id++;
    impl_->sinks[id] = std::make_pair(options, sink);
    sink->OnSourceStateChanged(options.stream_id, StreamState::kRunning);
    return infra::Result<FrameSubscriptionId>::Ok(id);
}

infra::Status MediaService::UnsubscribeFrames(
    FrameSubscriptionId subscription_id) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    auto it = impl_->sinks.find(subscription_id);
    if (it == impl_->sinks.end()) {
        return infra::Status::kNotFound;
    }
    impl_->sinks.erase(it);
    return infra::Status::kOk;
}

infra::Status MediaService::RequestKeyFrame(infra::StreamId stream_id,
                                           KeyFrameReason reason) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    (void)reason;
    if (!IsValidMediaStream(stream_id)) {
        return infra::Status::kInvalidParam;
    }
    if (impl_->state != ServiceState::kStarted) {
        return infra::Status::kBusy;
    }
    return hisisdk::DefaultSdk().RequestIdr(
        impl_->pipeline.config().venc_channel);
}

infra::Result<MediaCapabilities> MediaService::GetCapabilities() const {
    if (impl_ == nullptr) {
        return infra::Result<MediaCapabilities>::Fail(
            infra::Status::kInternalError);
    }
    return impl_->pipeline.GetCapabilities();
}

infra::Status MediaService::SetEncodedFrameCallback(
    EncodedFrameCallback callback,
    void* user) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    if (impl_->state == ServiceState::kStarted) {
        return infra::Status::kBusy;
    }
    impl_->callback = callback;
    impl_->callback_user = user;
    return infra::Status::kOk;
}

infra::Result<MediaChannels> MediaService::GetChannels() const {
    if (impl_ == nullptr) {
        return infra::Result<MediaChannels>::Fail(
            infra::Status::kInternalError);
    }
    if (!impl_->pipeline.system_initialized()) {
        return infra::Result<MediaChannels>::Fail(infra::Status::kBusy);
    }
    return infra::Result<MediaChannels>::Ok(impl_->pipeline.channels());
}

infra::Result<MppChannel> MediaService::GetMainVpssChannel() const {
    const infra::Result<MediaChannels> result = GetChannels();
    if (!result.IsOk()) {
        return infra::Result<MppChannel>::Fail(result.status);
    }
    return infra::Result<MppChannel>::Ok(result.value.vpss);
}

infra::Result<MppChannel> MediaService::GetMainVencChannel() const {
    const infra::Result<MediaChannels> result = GetChannels();
    if (!result.IsOk()) {
        return infra::Result<MppChannel>::Fail(result.status);
    }
    return infra::Result<MppChannel>::Ok(result.value.venc);
}

}  // namespace live_stream
