#include "media_service.h"

#include "config_service.h"
#include "hisisdk/hisi_sdk.h"
#include "infra/clamp.h"
#include "infra/log.h"
#include "json_utils.h"
#include "media_config_codec.h"
#include "media/media_buffer.h"
#include "media_pipeline.h"
#include "stream_codec.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
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

using media_internal::ParseVideoConfig;
using media_internal::ValidateImageConfig;

constexpr int32_t kControlMin = 0;
constexpr int32_t kControlMax = 100;
constexpr int kImageStrategyIntervalMs = 1000;

const char* StreamName(StreamId stream_id) {
    switch (stream_id) {
        case StreamId::kMain:
            return "main";
        case StreamId::kSub:
            return "sub";
        case StreamId::kSnapshot:
            return "snapshot";
    }
    return "unknown";
}

int IsoTier(uint32_t iso) {
    if (iso <= 400) {
        return 0;
    }
    if (iso <= 1600) {
        return 1;
    }
    if (iso <= 6400) {
        return 2;
    }
    return 3;
}

const char *IsoTierName(int tier) {
    switch (tier) {
        case 0:
            return "day";
        case 1:
            return "indoor";
        case 2:
            return "low_light";
        case 3:
            return "very_low_light";
    }
    return "unknown";
}

struct ImageStrategyControls {
    int32_t saturation = 52;
    int32_t sharpness = 42;
    int32_t denoise_2d = 60;
    int32_t denoise_3d = 52;
    int32_t gamma = 50;
};

int32_t ClampImageControl(int32_t value) {
    return infra::Clamp(value, kControlMin, kControlMax);
}

ImageStrategyControls LoadImageStrategyControls(
    const ConfigJson &image_config) {
    ImageStrategyControls controls;
    const ConfigJson &basic = image_config.at("basic");
    const ConfigJson &enhancement = image_config.at("enhancement");
    (void)json_utils::ReadField(basic, "saturation", &controls.saturation,
                                kControlMin, kControlMax);
    (void)json_utils::ReadField(basic, "sharpness", &controls.sharpness,
                                kControlMin, kControlMax);
    (void)json_utils::ReadField(enhancement, "denoise_2d",
                                &controls.denoise_2d, kControlMin,
                                kControlMax);
    (void)json_utils::ReadField(enhancement, "denoise_3d",
                                &controls.denoise_3d, kControlMin,
                                kControlMax);
    (void)json_utils::ReadField(enhancement, "gamma", &controls.gamma,
                                kControlMin, kControlMax);
    return controls;
}

ImageStrategyControls ControlsForIsoTier(
    const ImageStrategyControls &base,
    const std::string &mode,
    int tier) {
    int32_t saturation_delta[] = {0, 0, -6, -14};
    int32_t sharpness_delta[] = {0, -6, -14, -24};
    int32_t denoise_2d_delta[] = {0, 8, 18, 30};
    int32_t denoise_3d_delta[] = {0, 10, 22, 34};
    int32_t gamma_delta[] = {0, 2, 5, 8};
    if (mode == "low_noise") {
        saturation_delta[0] = 0;
        saturation_delta[1] = -4;
        saturation_delta[2] = -10;
        saturation_delta[3] = -18;
        sharpness_delta[0] = -6;
        sharpness_delta[1] = -12;
        sharpness_delta[2] = -22;
        sharpness_delta[3] = -32;
        denoise_2d_delta[0] = 8;
        denoise_2d_delta[1] = 16;
        denoise_2d_delta[2] = 30;
        denoise_2d_delta[3] = 42;
        denoise_3d_delta[0] = 10;
        denoise_3d_delta[1] = 22;
        denoise_3d_delta[2] = 38;
        denoise_3d_delta[3] = 50;
        gamma_delta[0] = 0;
        gamma_delta[1] = 1;
        gamma_delta[2] = 4;
        gamma_delta[3] = 7;
    } else if (mode == "detail") {
        saturation_delta[0] = 5;
        saturation_delta[1] = 2;
        saturation_delta[2] = -4;
        saturation_delta[3] = -10;
        sharpness_delta[0] = 8;
        sharpness_delta[1] = 2;
        sharpness_delta[2] = -8;
        sharpness_delta[3] = -18;
        denoise_2d_delta[0] = -6;
        denoise_2d_delta[1] = 2;
        denoise_2d_delta[2] = 12;
        denoise_2d_delta[3] = 24;
        denoise_3d_delta[0] = -4;
        denoise_3d_delta[1] = 4;
        denoise_3d_delta[2] = 16;
        denoise_3d_delta[3] = 28;
        gamma_delta[0] = 0;
        gamma_delta[1] = 3;
        gamma_delta[2] = 7;
        gamma_delta[3] = 10;
    }
    ImageStrategyControls controls;
    controls.saturation =
        ClampImageControl(base.saturation + saturation_delta[tier]);
    controls.sharpness =
        ClampImageControl(base.sharpness + sharpness_delta[tier]);
    controls.denoise_2d =
        ClampImageControl(base.denoise_2d + denoise_2d_delta[tier]);
    controls.denoise_3d =
        ClampImageControl(base.denoise_3d + denoise_3d_delta[tier]);
    controls.gamma = ClampImageControl(base.gamma + gamma_delta[tier]);
    return controls;
}

ImageStrategyControls SmoothImageStrategyControls(
    const ImageStrategyControls &target,
    const ImageStrategyStatus &current) {
    ImageStrategyControls controls = target;
    if (!current.active) {
        return controls;
    }
    controls.saturation =
        current.saturation + (target.saturation - current.saturation) / 3;
    controls.sharpness =
        current.sharpness + (target.sharpness - current.sharpness) / 3;
    controls.denoise_2d =
        current.denoise_2d + (target.denoise_2d - current.denoise_2d) / 3;
    controls.denoise_3d =
        current.denoise_3d + (target.denoise_3d - current.denoise_3d) / 3;
    controls.gamma = current.gamma + (target.gamma - current.gamma) / 3;
    return controls;
}

bool IsImageStrategyEnabled(const ConfigJson &image_config) {
    const auto strategy = image_config.find("strategy");
    if (strategy == image_config.end() || !strategy->is_object()) {
        return true;
    }
    return strategy->value("enabled", true);
}

std::string ImageStrategyMode(const ConfigJson &image_config) {
    const auto strategy = image_config.find("strategy");
    if (strategy == image_config.end() || !strategy->is_object()) {
        return "balanced";
    }
    return strategy->value("mode", std::string("balanced"));
}

const VideoStreamConfig *FindConfiguredStream(
    const MediaPipelineConfig &config,
    StreamId stream_id) {
    if (stream_id == config.main_stream.stream_id) {
        return &config.main_stream;
    }
    if (stream_id == config.sub_stream.stream_id) {
        return &config.sub_stream;
    }
    return nullptr;
}

int32_t VencChannelForStream(const MediaPipelineConfig &config,
                             StreamId stream_id) {
    if (stream_id == config.sub_stream.stream_id) {
        return config.sub_venc_channel;
    }
    if (stream_id == config.main_stream.stream_id) {
        return config.venc_channel;
    }
    return -1;
}

MediaChannels BuildChannelsForConfig(const MediaPipelineConfig &config) {
    MediaChannels channels;
    channels.vi = MppChannel{MppModule::kVi, config.video_pipe,
                             config.vi_channel};
    channels.vpss = MppChannel{MppModule::kVpss, config.vpss_group,
                               config.vpss_channel};
    channels.sub_vpss = MppChannel{MppModule::kVpss, config.vpss_group,
                                   config.sub_vpss_channel};
    channels.venc = MppChannel{MppModule::kVenc, 0, config.venc_channel};
    channels.sub_venc =
        MppChannel{MppModule::kVenc, 0, config.sub_venc_channel};
    channels.video_pipe = config.video_pipe;
    channels.snap_pipe = config.snap_pipe;
    channels.main_size = config.main_stream.size;
    channels.sub_size = config.sub_stream.size;
    return channels;
}

bool IsValidSnapshotVencChannel(const MediaPipelineConfig &config) {
    const hisisdk::SnapshotConfig snapshot;
    if (snapshot.jpeg_venc_channel == config.venc_channel) {
        return false;
    }
    return !config.sub_stream.enabled ||
           snapshot.jpeg_venc_channel != config.sub_venc_channel;
}

bool EncodedFrameHasCompleteParameterSets(const EncodedFrame &frame) {
    const uint8_t *data = frame.PayloadData();
    if (data == nullptr ||
        (frame.codec != VideoCodec::kH264 && frame.codec != VideoCodec::kH265)) {
        return false;
    }

    if (frame.codec == VideoCodec::kH265) {
        stream_codec::H265NalUnitList units;
        return stream_codec::ParseH265AnnexBNalUnits(data, frame.size, &units) &&
               stream_codec::HasCompleteH265ParameterSets(units);
    }

    stream_codec::H264NalUnitList units;
    return stream_codec::ParseH264AnnexBNalUnits(data, frame.size, &units) &&
           stream_codec::HasCompleteH264ParameterSets(units);
}

bool EncodedFrameHasKeyPicture(const EncodedFrame &frame) {
    if (stream_codec::IsKeyFrame(frame.frame_type) ||
        frame.frame_type == FrameType::kJpeg) {
        return true;
    }
    const uint8_t *data = frame.PayloadData();
    if (data == nullptr) {
        return false;
    }

    if (frame.codec == VideoCodec::kH265) {
        stream_codec::H265NalUnitList units;
        return stream_codec::ParseH265AnnexBNalUnits(data, frame.size, &units) &&
               stream_codec::HasH265KeyFrame(units);
    }
    if (frame.codec == VideoCodec::kH264) {
        stream_codec::H264NalUnitList units;
        return stream_codec::ParseH264AnnexBNalUnits(data, frame.size, &units) &&
               stream_codec::HasH264KeyFrame(units);
    }
    return false;
}

EncodedFrame CloneEncodedFramePayload(const EncodedFrame &frame) {
    EncodedFrame copy;
    copy.stream_id = frame.stream_id;
    copy.codec = frame.codec;
    copy.frame_type = frame.frame_type;
    copy.sequence = frame.sequence;
    copy.pts_us = frame.pts_us;
    copy.dts_us = frame.dts_us;
    const uint8_t *payload = frame.PayloadData();
    if (payload == nullptr || frame.size == 0) {
        return copy;
    }
    VideoBuffer *buffer = VideoBufferAlloc(frame.size);
    if (buffer == nullptr) {
        INFRA_LOG_ERROR("media_service",
                        "clone key frame alloc failed stream=%s seq=%llu "
                        "size=%u",
                        StreamName(frame.stream_id),
                        static_cast<unsigned long long>(frame.sequence),
                        frame.size);
        return EncodedFrame{};
    }
    std::memcpy(buffer->data, payload, frame.size);
    if (!VideoBufferSetSize(buffer, frame.size)) {
        VideoBufferRelease(buffer);
        return EncodedFrame{};
    }
    copy.buffer = buffer;
    copy.offset = 0;
    copy.size = frame.size;
    return copy;
}

class KeyFrameCache {
public:
    void Remember(const EncodedFrame &frame) {
        if (!EncodedFrameHasKeyPicture(frame)) {
            return;
        }
        const bool has_parameter_sets =
            EncodedFrameHasCompleteParameterSets(frame);
        CachedKeyFrame *cached = FindMutable(frame.stream_id);
        if (cached == nullptr) {
            return;
        }
        if (cached->has_frame && cached->has_parameter_sets &&
            !has_parameter_sets) {
            return;
        }
        EncodedFrame cached_frame = CloneEncodedFramePayload(frame);
        if (!cached_frame.HasValidPayload()) {
            return;
        }
        cached->frame = std::move(cached_frame);
        cached->has_frame = true;
        cached->has_parameter_sets = has_parameter_sets;
    }

    void Clear() {
        main_ = CachedKeyFrame{};
        sub_ = CachedKeyFrame{};
    }

    bool Get(StreamId stream_id, EncodedFrame *frame) const {
        if (frame == nullptr) {
            return false;
        }
        const CachedKeyFrame *cached = Find(stream_id);
        if (cached == nullptr || !cached->has_frame) {
            return false;
        }
        *frame = cached->frame;
        return true;
    }

private:
    struct CachedKeyFrame {
        EncodedFrame frame;
        bool has_frame = false;
        bool has_parameter_sets = false;
    };

    CachedKeyFrame *FindMutable(StreamId stream_id) {
        if (stream_id == StreamId::kMain) {
            return &main_;
        }
        if (stream_id == StreamId::kSub) {
            return &sub_;
        }
        return nullptr;
    }

    const CachedKeyFrame *Find(StreamId stream_id) const {
        if (stream_id == StreamId::kMain) {
            return &main_;
        }
        if (stream_id == StreamId::kSub) {
            return &sub_;
        }
        return nullptr;
    }

    CachedKeyFrame main_;
    CachedKeyFrame sub_;
};

class FrameAttachments {
public:
    struct AttachedSink {
        IFrameSink *sink = nullptr;
        StreamId stream_id = StreamId::kMain;
    };

    struct SourceStateNotice {
        IFrameSink *sink = nullptr;
        StreamId stream_id = StreamId::kMain;
        StreamState state = StreamState::kClosed;
    };

    FrameAttachId ReserveId() { return next_attach_id_++; }

    void Add(FrameAttachId id, const FrameAttachOptions &options,
             IFrameSink *sink) {
        sinks_[id] = std::make_pair(options, sink);
    }

    bool Remove(FrameAttachId id) {
        auto it = sinks_.find(id);
        if (it == sinks_.end()) {
            return false;
        }
        sinks_.erase(it);
        return true;
    }

    std::vector<IFrameSink *> CollectSinks(StreamId stream_id) const {
        std::vector<IFrameSink *> sinks;
        for (const auto &item : sinks_) {
            if (item.second.first.stream_id == stream_id &&
                item.second.second != nullptr) {
                sinks.push_back(item.second.second);
            }
        }
        return sinks;
    }

    std::vector<AttachedSink> CollectAttachedSinks() const {
        std::vector<AttachedSink> attached_sinks;
        for (const auto &item : sinks_) {
            if (item.second.second == nullptr) {
                continue;
            }
            AttachedSink attached_sink;
            attached_sink.sink = item.second.second;
            attached_sink.stream_id = item.second.first.stream_id;
            attached_sinks.push_back(attached_sink);
        }
        return attached_sinks;
    }

private:
    std::map<FrameAttachId,
             std::pair<FrameAttachOptions, IFrameSink *>>
        sinks_;
    FrameAttachId next_attach_id_ = 1;
};

}  // namespace

namespace {

class MediaServiceImpl : public IMediaService {
public:
    explicit MediaServiceImpl(const MediaServiceOptions &service_options)
        : options(service_options),
          pipeline(service_options.default_config, service_options.sdk) {
        active_config = pipeline.config();
        active_channels = BuildChannelsForConfig(active_config);
        capabilities = pipeline.GetCapabilities();
        pipeline.SetFrameCallback(&MediaServiceImpl::OnPipelineFrame, this);
    }

    ~MediaServiceImpl() override {
        Release();
    }

    bool Start() override;
    void Stop() override;
    bool IsStarted() const override;
    bool IsRestarting() const override;
    bool IsStreamStarted(StreamId stream_id) const override;
    VideoCodec GetStreamCodec(StreamId stream_id) const override;
    FrameAttachId AttachFrameSink(const FrameAttachOptions &options,
                                        IFrameSink *sink) override;
    bool DetachFrameSink(FrameAttachId attach_id) override;
    bool RequestKeyFrame(StreamId stream_id, KeyFrameReason reason) override;
    MediaCapabilities GetCapabilities() const override;
    MediaChannels GetChannels() const override;
    ImageStrategyStatus GetImageStrategyStatus() const override;

private:
    MediaServiceOptions options;
    MediaPipeline pipeline;
    MediaPipelineConfig active_config;
    MediaChannels active_channels;
    MediaCapabilities capabilities;
    ServiceState state = ServiceState::kCreated;
    FrameAttachments frame_attachments;
    ConfigJson image_config = ConfigJson::object();
    ImageStrategyStatus image_strategy_status;
    KeyFrameCache key_frame_cache;
    mutable std::mutex mutex;
    std::mutex pipeline_op_mutex;
    bool video_config_attached = false;
    bool image_config_attached = false;
    bool system_initialized = false;
    std::thread image_strategy_thread;
    bool image_strategy_running = false;
    bool image_strategy_stop = false;

    struct AttachedConfigs {
        bool video = false;
        bool image = false;
    };

    bool Prepare() {
        ConfigJson video_config;
        ConfigJson next_image_config;
        if (options.config_service != nullptr) {
            video_config = options.config_service->GetValue("video");
            next_image_config = options.config_service->GetValue("image");
        }

        MediaPipelineConfig startup_config;
        MediaCapabilities capabilities_snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state == ServiceState::kInitialized ||
                state == ServiceState::kStarted ||
                state == ServiceState::kStopped) {
                return true;
            }
            if (state != ServiceState::kCreated &&
                state != ServiceState::kDeinitialized) {
                return false;
            }
            startup_config = active_config;
            capabilities_snapshot = capabilities;
        }

        if (video_config.is_object()) {
            const ConfigResult result = ParseVideoConfig(
                video_config, startup_config, capabilities_snapshot,
                &startup_config);
            if (!result.ok) {
                return false;
            }
        }

        const bool has_image_config = next_image_config.is_object();
        if (has_image_config) {
            const ConfigResult result =
                ValidateImageConfig(next_image_config,
                                    capabilities_snapshot.image);
            if (!result.ok) {
                return false;
            }
        }

        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex);
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state == ServiceState::kInitialized ||
                state == ServiceState::kStarted ||
                state == ServiceState::kStopped) {
                return true;
            }
            if (state != ServiceState::kCreated &&
                state != ServiceState::kDeinitialized) {
                return false;
            }
            state = ServiceState::kStopping;
            key_frame_cache.Clear();
        }

        pipeline.SetConfig(startup_config);
        if (!pipeline.InitSystem()) {
            pipeline.DeinitSystem();
            std::lock_guard<std::mutex> lock(mutex);
            state = ServiceState::kDeinitialized;
            system_initialized = false;
            return false;
        }

        AttachedConfigs attached_now;
        if (!AttachConfigs(&attached_now)) {
            pipeline.DeinitSystem();
            std::lock_guard<std::mutex> lock(mutex);
            state = ServiceState::kDeinitialized;
            system_initialized = false;
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            active_config = startup_config;
            active_channels = BuildChannelsForConfig(active_config);
            system_initialized = true;
            if (has_image_config) {
                image_config = next_image_config;
            }
            if (attached_now.video) {
                video_config_attached = true;
            }
            if (attached_now.image) {
                image_config_attached = true;
            }
            state = ServiceState::kInitialized;
        }
        return true;
    }

    bool AttachConfigs(AttachedConfigs *attached_now) {
        if (attached_now == nullptr) {
            return false;
        }
        *attached_now = AttachedConfigs{};
        if (options.config_service == nullptr) {
            return true;
        }

        bool need_video_attach = false;
        bool need_image_attach = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            need_video_attach = !video_config_attached;
            need_image_attach = !image_config_attached;
        }

        if (need_video_attach) {
            ConfigAttachment attachment;
            attachment.validate = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex);
                return CheckVideoConfig(value);
            };
            attachment.apply = [this](const ConfigJson &value) {
                return ApplyVideoConfig(value);
            };
            if (!options.config_service->AttachConfig("video", attachment)) {
                return false;
            }
            attached_now->video = true;
        }

        if (need_image_attach) {
            ConfigAttachment attachment;
            attachment.validate = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex);
                return CheckImageConfig(value);
            };
            attachment.apply = [this](const ConfigJson &value) {
                return ApplyImageConfig(value);
            };
            if (!options.config_service->AttachConfig("image", attachment)) {
                if (attached_now->video) {
                    (void)options.config_service->DetachConfig("video");
                }
                *attached_now = AttachedConfigs{};
                return false;
            }
            attached_now->image = true;
        }
        return true;
    }

    void Release() {
        StopImageStrategy();
        bool detach_video = false;
        bool detach_image = false;
        bool stop_pipeline = false;
        bool deinit_pipeline = false;
        std::vector<FrameAttachments::SourceStateNotice> source_state_events;
        {
            std::lock_guard<std::mutex> op_guard(pipeline_op_mutex);
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (state == ServiceState::kStarted) {
                    state = ServiceState::kStopping;
                    source_state_events =
                        BuildSourceStateEventsLocked(StreamState::kClosed);
                    stop_pipeline = true;
                    key_frame_cache.Clear();
                    state = ServiceState::kStopped;
                }
                if (state != ServiceState::kDeinitialized &&
                    state != ServiceState::kCreated) {
                    deinit_pipeline = true;
                    key_frame_cache.Clear();
                    state = ServiceState::kDeinitialized;
                }
                detach_video = video_config_attached;
                detach_image = image_config_attached;
                video_config_attached = false;
                image_config_attached = false;
                system_initialized = false;
            }
            if (stop_pipeline) {
                pipeline.Stop();
            }
            if (deinit_pipeline) {
                pipeline.DeinitSystem();
            }
        }
        NotifySourceState(source_state_events);

        if (options.config_service != nullptr) {
            if (detach_video) {
                (void)options.config_service->DetachConfig("video");
            }
            if (detach_image) {
                (void)options.config_service->DetachConfig("image");
            }
        }
    }

    static void OnPipelineFrame(const EncodedFrame &frame, void *user) {
        if (user != nullptr) {
            static_cast<MediaServiceImpl *>(user)->DispatchFrame(frame);
        }
    }

    void DispatchFrame(const EncodedFrame &frame) {
        FramePayload payload;
        payload.encoded_frame = frame;
        std::vector<IFrameSink *> matching_sinks;
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (state != ServiceState::kStarted) {
                return;
            }
            key_frame_cache.Remember(frame);
            matching_sinks = frame_attachments.CollectSinks(frame.stream_id);
        }
        for (IFrameSink *sink : matching_sinks) {
            sink->OnFrame(payload);
        }
    }

    std::vector<FrameAttachments::SourceStateNotice>
    BuildSourceStateEventsLocked(StreamState stream_state) const {
        std::vector<FrameAttachments::SourceStateNotice> events;
        const std::vector<FrameAttachments::AttachedSink> targets =
            frame_attachments.CollectAttachedSinks();
        for (const FrameAttachments::AttachedSink &target : targets) {
            const VideoStreamConfig *stream =
                FindConfiguredStream(active_config, target.stream_id);
            const bool stream_running =
                stream_state == StreamState::kRunning && stream != nullptr &&
                stream->enabled;
            FrameAttachments::SourceStateNotice event;
            event.sink = target.sink;
            event.stream_id = target.stream_id;
            event.state = stream_running ? stream_state : StreamState::kClosed;
            events.push_back(event);
        }
        return events;
    }

    static void NotifySourceState(
        const std::vector<FrameAttachments::SourceStateNotice>
            &source_state_events) {
        for (const FrameAttachments::SourceStateNotice &notification :
             source_state_events) {
            if (notification.sink != nullptr) {
                notification.sink->OnSourceStateChanged(notification.stream_id,
                                                        notification.state);
            }
        }
    }

    ConfigResult CheckVideoConfig(const ConfigJson &value) const {
        MediaPipelineConfig parsed;
        ConfigResult result =
            ParseVideoConfig(value, active_config, capabilities, &parsed);
        if (!result.ok) {
            return result;
        }
        return IsValidSnapshotVencChannel(parsed)
                   ? ConfigResult::Success()
                   : ConfigResult::Failure("streams",
                                           "snapshot VENC channel conflicts");
    }

    ConfigResult ApplyVideoConfig(const ConfigJson &value) {
        MediaPipelineConfig next_config;
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (state == ServiceState::kStopping) {
                return ConfigResult::Failure("", "media pipeline busy");
            }
            const ConfigResult result = ParseVideoConfig(
                value, active_config, capabilities, &next_config);
            if (!result.ok) {
                return result;
            }
            if (!IsValidSnapshotVencChannel(next_config)) {
                return ConfigResult::Failure(
                    "streams", "snapshot VENC channel conflicts");
            }
        }
        if (!ApplyPipelineConfig(next_config)) {
            return ConfigResult::Failure("streams.main", "apply failed");
        }
        return ConfigResult::Success();
    }

    ConfigResult CheckImageConfig(const ConfigJson &value) const {
        return ValidateImageConfig(value, capabilities.image);
    }

    ConfigResult ApplyImageConfig(const ConfigJson &value) {
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (state != ServiceState::kStarted) {
                image_config = value;
                return ConfigResult::Success();
            }
        }

        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex);
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (state != ServiceState::kStarted) {
                image_config = value;
                return ConfigResult::Success();
            }
        }
        if (!pipeline.ApplyImageConfig(value)) {
            return ConfigResult::Failure("image", "apply failed");
        }
        {
            std::lock_guard<std::mutex> guard(mutex);
            image_config = value;
        }
        return ConfigResult::Success();
    }

    bool ApplyImageConfigToPipeline(const ConfigJson &value) {
        if (!value.is_object() || value.empty()) {
            return true;
        }
        return pipeline.ApplyImageConfig(value);
    }

    ConfigJson BuildImageStrategyConfigLocked(
        const hisisdk::ExposureInfo &exposure,
        ImageStrategyStatus *next_status) const {
        ConfigJson adjusted = image_config;
        if (!adjusted.is_object()) {
            return adjusted;
        }

        const int tier = IsoTier(exposure.iso);
        const std::string strategy_mode = ImageStrategyMode(image_config);
        const ImageStrategyControls controls = SmoothImageStrategyControls(
            ControlsForIsoTier(LoadImageStrategyControls(image_config),
                               strategy_mode, tier),
            image_strategy_status);

        adjusted["basic"]["saturation"] = controls.saturation;
        adjusted["basic"]["sharpness"] = controls.sharpness;
        adjusted["enhancement"]["denoise_2d"] = controls.denoise_2d;
        adjusted["enhancement"]["denoise_3d"] = controls.denoise_3d;
        adjusted["enhancement"]["gamma"] = controls.gamma;

        if (next_status != nullptr) {
            *next_status = image_strategy_status;
            next_status->enabled = true;
            next_status->active = true;
            next_status->exposure_valid = true;
            next_status->iso = exposure.iso;
            next_status->exposure_time_us = exposure.exposure_time_us;
            next_status->analog_gain = exposure.analog_gain;
            next_status->digital_gain = exposure.digital_gain;
            next_status->isp_digital_gain = exposure.isp_digital_gain;
            next_status->mode = strategy_mode;
            next_status->tier = IsoTierName(tier);
            next_status->saturation = controls.saturation;
            next_status->sharpness = controls.sharpness;
            next_status->denoise_2d = controls.denoise_2d;
            next_status->denoise_3d = controls.denoise_3d;
            next_status->gamma = controls.gamma;
        }
        return adjusted;
    }

    void StartImageStrategyLocked() {
        if (image_strategy_running) {
            return;
        }
        image_strategy_stop = false;
        image_strategy_running = true;
        image_strategy_thread =
            std::thread(&MediaServiceImpl::ImageStrategyLoop, this);
    }

    void StopImageStrategy() {
        {
            std::lock_guard<std::mutex> guard(mutex);
            image_strategy_stop = true;
        }
        if (image_strategy_thread.joinable()) {
            image_strategy_thread.join();
        }
        std::lock_guard<std::mutex> guard(mutex);
        image_strategy_running = false;
        image_strategy_stop = false;
        image_strategy_status.active = false;
    }

    void ImageStrategyLoop() {
        while (true) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                if (image_strategy_stop) {
                    return;
                }
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kImageStrategyIntervalMs));

            {
                std::lock_guard<std::mutex> guard(mutex);
                if (image_strategy_stop) {
                    return;
                }
                const bool strategy_enabled =
                    IsImageStrategyEnabled(image_config);
                image_strategy_status.enabled = strategy_enabled;
                if (state != ServiceState::kStarted || !strategy_enabled) {
                    image_strategy_status.active = false;
                    continue;
                }
            }

            const hisisdk::ExposureInfo exposure = pipeline.QueryExposureInfo();
            if (!exposure.valid) {
                std::lock_guard<std::mutex> guard(mutex);
                if (image_strategy_stop) {
                    return;
                }
                image_strategy_status.exposure_valid = false;
                continue;
            }

            ImageStrategyStatus next_status;
            ConfigJson adjusted;
            {
                std::lock_guard<std::mutex> guard(mutex);
                if (image_strategy_stop || state != ServiceState::kStarted ||
                    !IsImageStrategyEnabled(image_config)) {
                    continue;
                }
                adjusted = BuildImageStrategyConfigLocked(exposure,
                                                          &next_status);
            }

            bool applied = false;
            {
                std::lock_guard<std::mutex> op_guard(pipeline_op_mutex);
                bool can_apply = false;
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    can_apply =
                        !image_strategy_stop &&
                        state == ServiceState::kStarted &&
                        IsImageStrategyEnabled(image_config);
                }
                if (can_apply) {
                    applied = pipeline.ApplyImageConfig(adjusted);
                }
            }
            if (applied) {
                std::lock_guard<std::mutex> guard(mutex);
                image_strategy_status = next_status;
            }
        }
    }

    bool ApplyPipelineConfig(const MediaPipelineConfig &config) {
        bool restart_stream = false;
        bool rebuild_system = false;
        ServiceState state_before_change = ServiceState::kCreated;
        ConfigJson image_config_before_change;
        std::vector<FrameAttachments::SourceStateNotice>
            source_closed_events;
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (state == ServiceState::kStopping) {
                return false;
            }
            state_before_change = state;
            restart_stream = state == ServiceState::kStarted;
            rebuild_system = system_initialized;
            image_config_before_change = image_config;
            state = ServiceState::kStopping;
            if (restart_stream) {
                source_closed_events =
                    BuildSourceStateEventsLocked(StreamState::kClosed);
            }
            key_frame_cache.Clear();
        }
        NotifySourceState(source_closed_events);

        if (restart_stream) {
            StopImageStrategy();
        }

        bool device_config_applied = false;
        {
            std::lock_guard<std::mutex> op_guard(pipeline_op_mutex);
            if (restart_stream) {
                pipeline.Stop();
            }
            if (rebuild_system) {
                pipeline.DeinitSystem();
            }

            pipeline.SetConfig(config);

            device_config_applied = true;
            if (rebuild_system && !pipeline.InitSystem()) {
                device_config_applied = false;
            } else if (restart_stream && !pipeline.Start()) {
                device_config_applied = false;
            } else if (restart_stream &&
                       !ApplyImageConfigToPipeline(
                           image_config_before_change)) {
                device_config_applied = false;
            }

            if (!device_config_applied && (restart_stream || rebuild_system)) {
                pipeline.Stop();
                if (rebuild_system) {
                    pipeline.DeinitSystem();
                }
            }
        }

        std::vector<FrameAttachments::SourceStateNotice>
            source_state_events;
        if (device_config_applied) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                active_config = config;
                active_channels = BuildChannelsForConfig(active_config);
                system_initialized = rebuild_system;
                state = restart_stream ? ServiceState::kStarted
                                       : state_before_change;
                if (restart_stream) {
                    source_state_events =
                        BuildSourceStateEventsLocked(StreamState::kRunning);
                    StartImageStrategyLocked();
                }
            }
            NotifySourceState(source_state_events);
            return true;
        }

        {
            std::lock_guard<std::mutex> guard(mutex);
            system_initialized = false;
            if (restart_stream) {
                state = rebuild_system ? ServiceState::kDeinitialized
                                       : ServiceState::kStopped;
                source_state_events =
                    BuildSourceStateEventsLocked(StreamState::kError);
            } else {
                state = rebuild_system ? ServiceState::kDeinitialized
                                       : state_before_change;
            }
        }
        NotifySourceState(source_state_events);
        return false;
    }

};

bool MediaServiceImpl::Start() {
    bool need_init = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        need_init = state == ServiceState::kCreated ||
                    state == ServiceState::kDeinitialized;
    }
    if (need_init && !Prepare()) {
        return false;
    }

    ConfigJson image_config_before_change;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (state == ServiceState::kStarted) {
            return true;
        }
        if (state == ServiceState::kStopped) {
            state = ServiceState::kInitialized;
        }
        if (state != ServiceState::kInitialized) {
            return false;
        }
        state = ServiceState::kStopping;
        key_frame_cache.Clear();
        image_config_before_change = image_config;
    }

    bool ok = false;
    {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex);
        ok = pipeline.Start();
        if (ok) {
            ok = ApplyImageConfigToPipeline(image_config_before_change);
        }
        if (!ok) {
            pipeline.Stop();
        }
    }
    if (!ok) {
        std::vector<FrameAttachments::SourceStateNotice> source_state_events;
        {
            std::lock_guard<std::mutex> lock(mutex);
            key_frame_cache.Clear();
            state = ServiceState::kInitialized;
            source_state_events =
                BuildSourceStateEventsLocked(StreamState::kError);
        }
        NotifySourceState(source_state_events);
        return false;
    }
    std::vector<FrameAttachments::SourceStateNotice> source_state_events;
    {
        std::lock_guard<std::mutex> lock(mutex);
        state = ServiceState::kStarted;
        source_state_events = BuildSourceStateEventsLocked(StreamState::kRunning);
        StartImageStrategyLocked();
    }
    NotifySourceState(source_state_events);
    return true;
}

void MediaServiceImpl::Stop() {
    StopImageStrategy();
    bool should_stop = false;
    std::vector<FrameAttachments::SourceStateNotice> source_state_events;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (state != ServiceState::kStarted) {
            if (state == ServiceState::kStopping) {
                state = ServiceState::kStopped;
            }
            return;
        }

        state = ServiceState::kStopping;
        source_state_events = BuildSourceStateEventsLocked(StreamState::kClosed);
        key_frame_cache.Clear();
        should_stop = true;
    }
    NotifySourceState(source_state_events);

    if (should_stop) {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex);
        pipeline.Stop();
    }
    std::lock_guard<std::mutex> lock(mutex);
    state = ServiceState::kStopped;
}

bool MediaServiceImpl::IsStarted() const {
    std::lock_guard<std::mutex> lock(mutex);
    return state == ServiceState::kStarted;
}

bool MediaServiceImpl::IsRestarting() const {
    std::lock_guard<std::mutex> lock(mutex);
    return state == ServiceState::kStopping;
}

bool MediaServiceImpl::IsStreamStarted(StreamId stream_id) const {
    std::lock_guard<std::mutex> lock(mutex);
    const VideoStreamConfig *stream =
        FindConfiguredStream(active_config, stream_id);
    return state == ServiceState::kStarted && stream != nullptr &&
           stream->enabled;
}

VideoCodec MediaServiceImpl::GetStreamCodec(StreamId stream_id) const {
    std::lock_guard<std::mutex> lock(mutex);
    const VideoStreamConfig *stream =
        FindConfiguredStream(active_config, stream_id);
    if (stream != nullptr) {
        return stream->codec;
    }
    return VideoCodec::kH264;
}

FrameAttachId
MediaServiceImpl::AttachFrameSink(const FrameAttachOptions &options,
                                  IFrameSink *sink) {
    FrameAttachId id = 0;
    EncodedFrame last_key_frame;
    bool has_last_key_frame = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        const VideoStreamConfig *stream =
            FindConfiguredStream(active_config, options.stream_id);
        if (sink == nullptr || stream == nullptr || !stream->enabled ||
            state != ServiceState::kStarted) {
            return 0;
        }
        has_last_key_frame =
            options.require_key_frame_first &&
            key_frame_cache.Get(options.stream_id, &last_key_frame);
        id = frame_attachments.ReserveId();
        frame_attachments.Add(id, options, sink);
    }
    sink->OnSourceStateChanged(options.stream_id, StreamState::kRunning);
    if (has_last_key_frame) {
        FramePayload payload;
        payload.encoded_frame = last_key_frame;
        sink->OnFrame(payload);
    }
    return id;
}

bool MediaServiceImpl::DetachFrameSink(FrameAttachId attach_id) {
    std::lock_guard<std::mutex> lock(mutex);
    return frame_attachments.Remove(attach_id);
}

bool MediaServiceImpl::RequestKeyFrame(StreamId stream_id,
                                       KeyFrameReason reason) {
    (void)reason;
    int32_t venc_channel = -1;
    hisisdk::IHisiSdk *sdk = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        const MediaPipelineConfig config = active_config;
        const VideoStreamConfig *stream =
            FindConfiguredStream(config, stream_id);
        if (state != ServiceState::kStarted || stream == nullptr ||
            !stream->enabled) {
            return false;
        }
        venc_channel = VencChannelForStream(config, stream_id);
        sdk = options.sdk != nullptr
                  ? options.sdk
                  : &hisisdk::DefaultSdk();
    }
    return sdk->RequestIdr(venc_channel);
}

MediaCapabilities MediaServiceImpl::GetCapabilities() const {
    std::lock_guard<std::mutex> lock(mutex);
    return capabilities;
}

MediaChannels MediaServiceImpl::GetChannels() const {
    std::lock_guard<std::mutex> lock(mutex);
    if (!system_initialized) {
        return MediaChannels{};
    }
    return active_channels;
}

ImageStrategyStatus MediaServiceImpl::GetImageStrategyStatus() const {
    std::lock_guard<std::mutex> lock(mutex);
    return image_strategy_status;
}

}  // namespace

std::unique_ptr<IMediaService> CreateMediaService() {
    return CreateMediaService(MediaServiceOptions{});
}

std::unique_ptr<IMediaService> CreateMediaService(
    const MediaPipelineConfig &config) {
    MediaServiceOptions options;
    options.default_config = config;
    return CreateMediaService(options);
}

std::unique_ptr<IMediaService> CreateMediaService(
    const MediaServiceOptions &options) {
    return std::unique_ptr<IMediaService>(new MediaServiceImpl(options));
}

}  // namespace live_stream
