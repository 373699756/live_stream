#include "media_service.h"

#include "config_service.h"
#include "hisisdk/hisi_sdk.h"
#include "media_pipeline.h"

#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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

bool ParseResolution(const std::string& text, VideoSize* size) {
    if (size == nullptr) {
        return false;
    }
    const std::string::size_type split = text.find('x');
    if (split == std::string::npos) {
        return false;
    }
    const int width = std::atoi(text.substr(0, split).c_str());
    const int height = std::atoi(text.substr(split + 1).c_str());
    if (width <= 0 || height <= 0) {
        return false;
    }
    size->width = static_cast<uint32_t>(width);
    size->height = static_cast<uint32_t>(height);
    return true;
}

infra::Result<infra::VideoCodec> ParseCodec(const std::string& codec) {
    if (codec == "h264") {
        return infra::Result<infra::VideoCodec>::Ok(infra::VideoCodec::kH264);
    }
    if (codec == "h265") {
        return infra::Result<infra::VideoCodec>::Ok(infra::VideoCodec::kH265);
    }
    if (codec == "jpeg") {
        return infra::Result<infra::VideoCodec>::Ok(infra::VideoCodec::kJpeg);
    }
    if (codec == "mjpeg") {
        return infra::Result<infra::VideoCodec>::Ok(infra::VideoCodec::kMjpeg);
    }
    return infra::Result<infra::VideoCodec>::Fail(infra::Status::kInvalidParam);
}

RateControlMode ParseRateControl(const std::string& rc_mode) {
    if (rc_mode == "vbr") {
        return RateControlMode::kVbr;
    }
    if (rc_mode == "fix_qp") {
        return RateControlMode::kFixQp;
    }
    return RateControlMode::kCbr;
}

GopMode ParseGopMode(const std::string& gop_mode) {
    if (gop_mode == "dual_p") {
        return GopMode::kDualP;
    }
    if (gop_mode == "smart_p") {
        return GopMode::kSmartP;
    }
    return GopMode::kNormalP;
}

infra::Result<MediaPipelineConfig> ParseVideoConfig(
    const ConfigJson& value, const MediaPipelineConfig& fallback) {
    if (!value.is_object() || !value.contains("streams") ||
        !value["streams"].is_object() || !value["streams"].contains("main")) {
        return infra::Result<MediaPipelineConfig>::Fail(
            infra::Status::kInvalidParam);
    }

    const ConfigJson& main = value["streams"]["main"];
    if (!main.is_object()) {
        return infra::Result<MediaPipelineConfig>::Fail(
            infra::Status::kInvalidParam);
    }

    MediaPipelineConfig config = fallback;
    config.main_stream.stream_id = infra::StreamId::kMain;
    infra::Result<infra::VideoCodec> codec =
        ParseCodec(main.value("codec", std::string("h265")));
    if (!codec.IsOk()) {
        return infra::Result<MediaPipelineConfig>::Fail(codec.status);
    }
    config.main_stream.codec = codec.value;
    if (!ParseResolution(main.value("resolution", std::string("1920x1080")),
                         &config.main_stream.size)) {
        return infra::Result<MediaPipelineConfig>::Fail(
            infra::Status::kInvalidParam);
    }
    const int fps = main.value("fps", 0);
    const int bitrate = main.value("bitrate_kbps", 0);
    const int gop = main.value("gop", 0);
    if (fps <= 0 || bitrate <= 0 || gop <= 0) {
        return infra::Result<MediaPipelineConfig>::Fail(
            infra::Status::kInvalidParam);
    }
    config.main_stream.frame_rate.source_fps = fps;
    config.main_stream.frame_rate.target_fps = fps;
    config.main_stream.bitrate_kbps = static_cast<uint32_t>(bitrate);
    config.main_stream.gop = static_cast<uint32_t>(gop);
    config.main_stream.rc_mode =
        ParseRateControl(main.value("rate_control", std::string("cbr")));
    config.main_stream.gop_mode =
        ParseGopMode(main.value("gop_mode", std::string("normal_p")));

    if (!IsValidMediaPipelineConfig(config)) {
        return infra::Result<MediaPipelineConfig>::Fail(
            infra::Status::kInvalidParam);
    }
    return infra::Result<MediaPipelineConfig>::Ok(config);
}

}  // namespace

struct MediaService::Impl {
    explicit Impl(const MediaServiceOptions& service_options)
        : options(service_options),
          pipeline(service_options.default_config, service_options.sdk) {
        pipeline.SetFrameCallback(&Impl::OnPipelineFrame, this);
    }

    MediaServiceOptions options;
    MediaPipeline pipeline;
    ServiceState state = ServiceState::kCreated;
    EncodedFrameCallback callback = nullptr;
    void* callback_user = nullptr;
    std::map<FrameSubscriptionId, std::pair<FrameSubscribeOptions, IFrameSink*>>
        sinks;
    FrameSubscriptionId next_subscription_id = 1;
    MediaServiceStats stats;
    mutable std::mutex mutex;
    bool config_callbacks_registered = false;

    static void OnPipelineFrame(const infra::EncodedFrame& frame, void* user) {
        if (user != nullptr) {
            static_cast<Impl*>(user)->DispatchFrame(frame);
        }
    }

    void DispatchFrame(const infra::EncodedFrame& frame) {
        EncodedFrameCallback frame_callback = nullptr;
        void* frame_callback_user = nullptr;
        std::vector<IFrameSink*> matching_sinks;
        {
            std::lock_guard<std::mutex> guard(mutex);
            frame_callback = callback;
            frame_callback_user = callback_user;
            for (const auto& item : sinks) {
                if (item.second.first.stream_id == frame.stream_id &&
                    item.second.second != nullptr) {
                    matching_sinks.push_back(item.second.second);
                }
            }
        }
        if (frame_callback != nullptr) {
            frame_callback(frame, frame_callback_user);
        }
        for (IFrameSink* sink : matching_sinks) {
            sink->OnFrame(frame);
        }
    }

    void NotifySourceState(StreamState stream_state) {
        for (const auto& item : sinks) {
            if (item.second.second != nullptr) {
                item.second.second->OnSourceStateChanged(
                    item.second.first.stream_id, stream_state);
            }
        }
    }

    infra::Status VerifyConfig(const ConfigJson& value) const {
        return ParseVideoConfig(value, pipeline.config()).status;
    }

    infra::Status ApplyConfig(const ConfigJson& value) {
        infra::Result<MediaPipelineConfig> parsed =
            ParseVideoConfig(value, pipeline.config());
        if (!parsed.IsOk()) {
            ++stats.config_apply_failed_count;
            return parsed.status;
        }
        return ApplyPipelineConfig(parsed.value);
    }

    infra::Status ApplyPipelineConfig(const MediaPipelineConfig& config) {
        if (!IsValidMediaPipelineConfig(config)) {
            ++stats.config_apply_failed_count;
            return infra::Status::kInvalidParam;
        }
        if (config.main_stream.size.width == pipeline.config().main_stream.size.width &&
            config.main_stream.size.height == pipeline.config().main_stream.size.height &&
            config.main_stream.codec == pipeline.config().main_stream.codec &&
            config.main_stream.frame_rate.target_fps ==
                pipeline.config().main_stream.frame_rate.target_fps &&
            config.main_stream.bitrate_kbps ==
                pipeline.config().main_stream.bitrate_kbps &&
            config.main_stream.gop == pipeline.config().main_stream.gop &&
            config.main_stream.rc_mode == pipeline.config().main_stream.rc_mode &&
            config.main_stream.gop_mode == pipeline.config().main_stream.gop_mode) {
            ++stats.config_apply_count;
            return infra::Status::kOk;
        }

        const bool was_started = state == ServiceState::kStarted;
        const bool was_initialized = pipeline.system_initialized();
        const MediaPipelineConfig previous = pipeline.config();
        if (was_started) {
            NotifySourceState(StreamState::kClosed);
            pipeline.Stop();
        }
        if (was_initialized) {
            pipeline.DeinitSystem();
        }

        pipeline.SetConfig(config);
        infra::Status status = was_initialized ? pipeline.InitSystem()
                                               : infra::Status::kOk;
        if (status == infra::Status::kOk && was_started) {
            status = pipeline.Start();
        }
        if (status == infra::Status::kOk) {
            ++stats.config_apply_count;
            if (was_started) {
                ++stats.restart_count;
                NotifySourceState(StreamState::kRunning);
            }
            return infra::Status::kOk;
        }

        pipeline.Stop();
        if (pipeline.system_initialized()) {
            pipeline.DeinitSystem();
        }
        pipeline.SetConfig(previous);
        if (was_initialized && pipeline.InitSystem() == infra::Status::kOk &&
            was_started && pipeline.Start() == infra::Status::kOk) {
            NotifySourceState(StreamState::kRunning);
        } else if (was_started) {
            state = ServiceState::kStopped;
            NotifySourceState(StreamState::kError);
        }
        ++stats.config_apply_failed_count;
        return status;
    }
};

MediaService::MediaService() : MediaService(MediaServiceOptions{}) {}

MediaService::MediaService(const MediaPipelineConfig& config)
    : MediaService(MediaServiceOptions{config, nullptr, nullptr}) {}

MediaService::MediaService(const MediaServiceOptions& options)
    : impl_(new Impl(options)) {}

MediaService::~MediaService() {
    Deinit();
    delete impl_;
    impl_ = nullptr;
}

infra::Status MediaService::Init() {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
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

    if (impl_->options.config_service != nullptr) {
        ConfigJson video_config;
        if (impl_->options.config_service->GetValue("video", &video_config) ==
            infra::Status::kOk) {
            const infra::Status apply_status = impl_->ApplyConfig(video_config);
            if (apply_status != infra::Status::kOk) {
                impl_->pipeline.DeinitSystem();
                return apply_status;
            }
        }
        if (!impl_->config_callbacks_registered) {
            infra::Status status =
                impl_->options.config_service->RegisterVerify(
                    "video", [this](const ConfigJson& value) {
                        std::lock_guard<std::mutex> guard(impl_->mutex);
                        return impl_->VerifyConfig(value);
                    });
            if (status != infra::Status::kOk) {
                impl_->pipeline.DeinitSystem();
                return status;
            }
            status = impl_->options.config_service->RegisterApply(
                "video", [this](const ConfigJson& value) {
                    std::lock_guard<std::mutex> guard(impl_->mutex);
                    return impl_->ApplyConfig(value);
                });
            if (status != infra::Status::kOk) {
                impl_->pipeline.DeinitSystem();
                return status;
            }
            impl_->config_callbacks_registered = true;
        }
    }

    impl_->state = ServiceState::kInitialized;
    return infra::Status::kOk;
}

infra::Status MediaService::Start() {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
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
    std::lock_guard<std::mutex> lock(impl_->mutex);
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
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == ServiceState::kStarted) {
        impl_->state = ServiceState::kStopping;
        impl_->NotifySourceState(StreamState::kClosed);
        impl_->pipeline.Stop();
        impl_->state = ServiceState::kStopped;
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
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (sink == nullptr || !IsValidMediaStream(options.stream_id)) {
        return infra::Result<FrameSubscriptionId>::Fail(
            infra::Status::kInvalidParam);
    }
    if (impl_->state != ServiceState::kStarted) {
        return infra::Result<FrameSubscriptionId>::Fail(infra::Status::kBusy);
    }

    const FrameSubscriptionId id = impl_->next_subscription_id++;
    impl_->sinks[id] = std::make_pair(options, sink);
    impl_->stats.subscription_count =
        static_cast<uint32_t>(impl_->sinks.size());
    sink->OnSourceStateChanged(options.stream_id, StreamState::kRunning);
    return infra::Result<FrameSubscriptionId>::Ok(id);
}

infra::Status MediaService::UnsubscribeFrames(
    FrameSubscriptionId subscription_id) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->sinks.find(subscription_id);
    if (it == impl_->sinks.end()) {
        return infra::Status::kNotFound;
    }
    impl_->sinks.erase(it);
    impl_->stats.subscription_count =
        static_cast<uint32_t>(impl_->sinks.size());
    return infra::Status::kOk;
}

infra::Status MediaService::RequestKeyFrame(infra::StreamId stream_id,
                                           KeyFrameReason reason) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    (void)reason;
    if (!IsValidMediaStream(stream_id)) {
        return infra::Status::kInvalidParam;
    }
    if (impl_->state != ServiceState::kStarted) {
        return infra::Status::kBusy;
    }
    hisisdk::IHisiSdk* sdk = impl_->options.sdk != nullptr
                                 ? impl_->options.sdk
                                 : &hisisdk::DefaultSdk();
    return sdk->RequestIdr(impl_->pipeline.config().venc_channel);
}

infra::Result<MediaCapabilities> MediaService::GetCapabilities() const {
    if (impl_ == nullptr) {
        return infra::Result<MediaCapabilities>::Fail(
            infra::Status::kInternalError);
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->pipeline.GetCapabilities();
}

infra::Status MediaService::SetEncodedFrameCallback(
    EncodedFrameCallback callback,
    void* user) {
    if (impl_ == nullptr) {
        return infra::Status::kInternalError;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
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
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->pipeline.system_initialized()) {
        return infra::Result<MediaChannels>::Fail(infra::Status::kBusy);
    }
    return infra::Result<MediaChannels>::Ok(impl_->pipeline.channels());
}

MediaServiceStats MediaService::GetStats() const {
    if (impl_ == nullptr) {
        return MediaServiceStats{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    MediaServiceStats stats = impl_->stats;
    stats.subscription_count = static_cast<uint32_t>(impl_->sinks.size());
    return stats;
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
