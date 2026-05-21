#include "media_service.h"

#include "config_service.h"
#include "hisisdk/hisi_sdk.h"
#include "infra/log.h"
#include "media_config_codec.h"
#include "media/media_buffer.h"
#include "media_pipeline.h"
#include "stream_codec.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
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

using media_internal::ParseVideoConfig;
using media_internal::ValidateImageConfig;

constexpr FrameSequence kMediaDiagMaxSeq = 8;

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
    FrameSubscriptionId SubscribeFrames(const FrameSubscribeOptions &options,
                                        IFrameSink *sink) override;
    bool UnsubscribeFrames(FrameSubscriptionId subscription_id) override;
    bool RequestKeyFrame(StreamId stream_id, KeyFrameReason reason) override;
    MediaCapabilities GetCapabilities() const override;
    MediaChannels GetChannels() const override;

private:
    MediaServiceOptions options;
    MediaPipeline pipeline;
    MediaPipelineConfig active_config;
    MediaChannels active_channels;
    MediaCapabilities capabilities;
    ServiceState state = ServiceState::kCreated;
    std::map<FrameSubscriptionId, std::pair<FrameSubscribeOptions, IFrameSink *>>
        sinks;
    FrameSubscriptionId next_subscription_id = 1;
    ConfigJson image_config = ConfigJson::object();
    EncodedFrame last_main_key_frame;
    EncodedFrame last_sub_key_frame;
    mutable std::mutex mutex;
    std::mutex pipeline_op_mutex;
    bool video_config_attached = false;
    bool image_config_attached = false;
    bool system_initialized = false;
    bool has_last_main_key_frame = false;
    bool has_last_sub_key_frame = false;
    bool last_main_key_frame_has_parameter_sets = false;
    bool last_sub_key_frame_has_parameter_sets = false;

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
            ClearKeyFrameCacheLocked();
        }

        pipeline.SetConfig(startup_config);
        if (!pipeline.InitSystem()) {
            pipeline.DeinitSystem();
            std::lock_guard<std::mutex> lock(mutex);
            state = ServiceState::kDeinitialized;
            system_initialized = false;
            return false;
        }

        bool attached_video_now = false;
        bool attached_image_now = false;
        if (options.config_service != nullptr) {
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
                    pipeline.DeinitSystem();
                    std::lock_guard<std::mutex> lock(mutex);
                    state = ServiceState::kDeinitialized;
                    system_initialized = false;
                    return false;
                }
                attached_video_now = true;
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
                    if (attached_video_now) {
                        (void)options.config_service->DetachConfig("video");
                    }
                    pipeline.DeinitSystem();
                    std::lock_guard<std::mutex> lock(mutex);
                    state = ServiceState::kDeinitialized;
                    system_initialized = false;
                    return false;
                }
                attached_image_now = true;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            active_config = startup_config;
            active_channels = BuildChannelsForConfig(active_config);
            system_initialized = true;
            if (has_image_config) {
                image_config = next_image_config;
            }
            if (attached_video_now) {
                video_config_attached = true;
            }
            if (attached_image_now) {
                image_config_attached = true;
            }
            state = ServiceState::kInitialized;
        }
        return true;
    }

    void Release() {
        bool detach_video = false;
        bool detach_image = false;
        bool stop_pipeline = false;
        bool deinit_pipeline = false;
        {
            std::lock_guard<std::mutex> op_guard(pipeline_op_mutex);
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (state == ServiceState::kStarted) {
                    state = ServiceState::kStopping;
                    NotifySourceState(StreamState::kClosed);
                    stop_pipeline = true;
                    ClearKeyFrameCacheLocked();
                    state = ServiceState::kStopped;
                }
                if (state != ServiceState::kDeinitialized &&
                    state != ServiceState::kCreated) {
                    deinit_pipeline = true;
                    ClearKeyFrameCacheLocked();
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
        if (frame.sequence <= kMediaDiagMaxSeq) {
            INFRA_LOG_INFO("media_service",
                           "media diag pipeline callback stream=%s seq=%llu "
                           "size=%u user=%p",
                           StreamName(frame.stream_id),
                           static_cast<unsigned long long>(frame.sequence),
                           frame.size, user);
        }
        if (user != nullptr) {
            static_cast<MediaServiceImpl *>(user)->DispatchFrame(frame);
        }
    }

    void DispatchFrame(const EncodedFrame &frame) {
        std::vector<IFrameSink *> matching_sinks;
        const bool log_diag = frame.sequence <= kMediaDiagMaxSeq;
        if (log_diag) {
            INFRA_LOG_INFO("media_service",
                           "media diag dispatch begin stream=%s seq=%llu "
                           "size=%u",
                           StreamName(frame.stream_id),
                           static_cast<unsigned long long>(frame.sequence),
                           frame.size);
        }
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (state != ServiceState::kStarted) {
                return;
            }
            if (log_diag) {
                INFRA_LOG_INFO(
                    "media_service",
                    "media diag remember begin stream=%s seq=%llu",
                    StreamName(frame.stream_id),
                    static_cast<unsigned long long>(frame.sequence));
            }
            RememberKeyFrame(frame);
            if (log_diag) {
                INFRA_LOG_INFO(
                    "media_service",
                    "media diag remember end stream=%s seq=%llu",
                    StreamName(frame.stream_id),
                    static_cast<unsigned long long>(frame.sequence));
            }
            for (const auto &item : sinks) {
                if (item.second.first.stream_id == frame.stream_id &&
                    item.second.second != nullptr) {
                    matching_sinks.push_back(item.second.second);
                }
            }
        }
        if (log_diag) {
            INFRA_LOG_INFO("media_service",
                           "media diag sinks begin stream=%s seq=%llu "
                           "sinks=%zu",
                           StreamName(frame.stream_id),
                           static_cast<unsigned long long>(frame.sequence),
                           matching_sinks.size());
        }
        for (IFrameSink *sink : matching_sinks) {
            if (log_diag) {
                INFRA_LOG_INFO(
                    "media_service",
                    "media diag sink call begin stream=%s seq=%llu sink=%p",
                    StreamName(frame.stream_id),
                    static_cast<unsigned long long>(frame.sequence),
                    static_cast<void*>(sink));
            }
            sink->OnFrame(frame);
            if (log_diag) {
                INFRA_LOG_INFO(
                    "media_service",
                    "media diag sink call end stream=%s seq=%llu sink=%p",
                    StreamName(frame.stream_id),
                    static_cast<unsigned long long>(frame.sequence),
                    static_cast<void*>(sink));
            }
        }
        if (log_diag) {
            INFRA_LOG_INFO("media_service",
                           "media diag dispatch end stream=%s seq=%llu",
                           StreamName(frame.stream_id),
                           static_cast<unsigned long long>(frame.sequence));
        }
    }

    void RememberKeyFrame(const EncodedFrame &frame) {
        if (!EncodedFrameHasKeyPicture(frame)) {
            return;
        }
        const bool has_parameter_sets =
            EncodedFrameHasCompleteParameterSets(frame);
        if (frame.stream_id == StreamId::kMain) {
            if (has_last_main_key_frame &&
                last_main_key_frame_has_parameter_sets &&
                !has_parameter_sets) {
                return;
            }
            EncodedFrame cached_frame = CloneEncodedFramePayload(frame);
            if (!cached_frame.HasValidPayload()) {
                return;
            }
            last_main_key_frame = std::move(cached_frame);
            has_last_main_key_frame = true;
            last_main_key_frame_has_parameter_sets = has_parameter_sets;
        } else if (frame.stream_id == StreamId::kSub) {
            if (has_last_sub_key_frame &&
                last_sub_key_frame_has_parameter_sets &&
                !has_parameter_sets) {
                return;
            }
            EncodedFrame cached_frame = CloneEncodedFramePayload(frame);
            if (!cached_frame.HasValidPayload()) {
                return;
            }
            last_sub_key_frame = std::move(cached_frame);
            has_last_sub_key_frame = true;
            last_sub_key_frame_has_parameter_sets = has_parameter_sets;
        }
    }

    void ClearKeyFrameCacheLocked() {
        last_main_key_frame = EncodedFrame{};
        last_sub_key_frame = EncodedFrame{};
        has_last_main_key_frame = false;
        has_last_sub_key_frame = false;
        last_main_key_frame_has_parameter_sets = false;
        last_sub_key_frame_has_parameter_sets = false;
    }

    bool GetLastKeyFrameLocked(StreamId stream_id, EncodedFrame *frame) const {
        if (frame == nullptr) {
            return false;
        }
        if (stream_id == StreamId::kMain && has_last_main_key_frame) {
            *frame = last_main_key_frame;
            return true;
        }
        if (stream_id == StreamId::kSub && has_last_sub_key_frame) {
            *frame = last_sub_key_frame;
            return true;
        }
        return false;
    }

    void NotifySourceState(StreamState stream_state) {
        for (const auto &item : sinks) {
            if (item.second.second != nullptr) {
                const VideoStreamConfig *stream =
                    FindConfiguredStream(active_config,
                                         item.second.first.stream_id);
                const bool stream_running =
                    stream_state == StreamState::kRunning && stream != nullptr &&
                    stream->enabled;
                const StreamState effective_state =
                    stream_running ? stream_state : StreamState::kClosed;
                item.second.second->OnSourceStateChanged(item.second.first.stream_id,
                                                         effective_state);
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

    bool ApplyPipelineConfig(const MediaPipelineConfig &config) {
        std::lock_guard<std::mutex> op_guard(pipeline_op_mutex);
        bool was_started = false;
        bool was_initialized = false;
        ServiceState previous_state = ServiceState::kCreated;
        MediaPipelineConfig previous;
        ConfigJson saved_image_config;
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (state == ServiceState::kStopping) {
                return false;
            }
            previous_state = state;
            was_started = state == ServiceState::kStarted;
            was_initialized = system_initialized;
            previous = active_config;
            saved_image_config = image_config;
            state = ServiceState::kStopping;
            if (was_started) {
                NotifySourceState(StreamState::kClosed);
            }
            ClearKeyFrameCacheLocked();
        }

        if (was_started) {
            pipeline.Stop();
        }
        if (was_initialized) {
            pipeline.DeinitSystem();
        }

        pipeline.SetConfig(config);
        bool ok = !was_initialized || pipeline.InitSystem();
        if (ok && was_started) {
            ok = pipeline.Start();
            if (ok) {
                ok = ApplyImageConfigToPipeline(saved_image_config);
            }
        }
        if (ok) {
            std::lock_guard<std::mutex> guard(mutex);
            active_config = config;
            active_channels = BuildChannelsForConfig(active_config);
            system_initialized = was_initialized;
            state = previous_state;
            if (was_started) {
                state = ServiceState::kStarted;
                NotifySourceState(StreamState::kRunning);
            }
            return true;
        }

        pipeline.Stop();
        if (was_initialized) {
            pipeline.DeinitSystem();
        }
        pipeline.SetConfig(previous);
        bool recovered = false;
        if (was_initialized) {
            recovered = pipeline.InitSystem();
            if (recovered && was_started) {
                recovered = pipeline.Start();
                if (recovered) {
                    recovered = ApplyImageConfigToPipeline(saved_image_config);
                }
            }
        } else {
            recovered = true;
        }
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (recovered) {
                active_config = previous;
                active_channels = BuildChannelsForConfig(active_config);
                system_initialized = was_initialized;
                state = previous_state;
                if (was_started) {
                    state = ServiceState::kStarted;
                    NotifySourceState(StreamState::kRunning);
                }
            } else if (was_started) {
                system_initialized = false;
                state = ServiceState::kStopped;
                NotifySourceState(StreamState::kError);
            } else {
                system_initialized = false;
                state = was_initialized ? ServiceState::kDeinitialized
                                        : previous_state;
            }
        }
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

    ConfigJson saved_image_config;
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
        ClearKeyFrameCacheLocked();
        saved_image_config = image_config;
    }

    std::lock_guard<std::mutex> op_guard(pipeline_op_mutex);
    bool ok = pipeline.Start();
    if (ok) {
        ok = ApplyImageConfigToPipeline(saved_image_config);
    }
    if (!ok) {
        pipeline.Stop();
        std::lock_guard<std::mutex> lock(mutex);
        ClearKeyFrameCacheLocked();
        state = ServiceState::kInitialized;
        NotifySourceState(StreamState::kError);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        state = ServiceState::kStarted;
        NotifySourceState(StreamState::kRunning);
    }
    return true;
}

void MediaServiceImpl::Stop() {
    bool should_stop = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (state != ServiceState::kStarted) {
            if (state == ServiceState::kStopping) {
                state = ServiceState::kStopped;
            }
            return;
        }

        state = ServiceState::kStopping;
        NotifySourceState(StreamState::kClosed);
        ClearKeyFrameCacheLocked();
        should_stop = true;
    }

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

FrameSubscriptionId
MediaServiceImpl::SubscribeFrames(const FrameSubscribeOptions &options,
                                  IFrameSink *sink) {
    FrameSubscriptionId id = 0;
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
            GetLastKeyFrameLocked(options.stream_id, &last_key_frame);
        id = next_subscription_id++;
    }
    sink->OnSourceStateChanged(options.stream_id, StreamState::kRunning);
    if (has_last_key_frame) {
        sink->OnFrame(last_key_frame);
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        sinks[id] = std::make_pair(options, sink);
    }
    return id;
}

bool MediaServiceImpl::UnsubscribeFrames(FrameSubscriptionId subscription_id) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = sinks.find(subscription_id);
    if (it == sinks.end()) {
        return false;
    }
    sinks.erase(it);
    return true;
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
