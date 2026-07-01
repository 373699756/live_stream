#include "hisi_vendor/mpp_sdk.h"
#include "hisi_mpp_venc_capture.h"
#include "hisi_mpp_venc_channel.h"
#include "mpp_hisi_sdk_impl.h"
#include "venc_config.h"

#include "infra/log.h"

#include <functional>
#include <thread>

namespace live_stream {
namespace hisisdk {
using venc_internal::ApplyVencRoiConfig;
using venc_internal::CodecName;
using venc_internal::GopModeName;
using venc_internal::RcModeName;
using venc_internal::VencChannelControl;
using venc_internal::VencStreamCapture;

// ====================================================================
// StartVenc / StopVenc
// ====================================================================
bool MppHisiSdk::StartVenc(const MediaPipelineConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    const bool sub_stream_enabled = config.sub_stream.enabled;
    const bool venc_channels_ready =
        VencChannelControl::Matches(impl_->main_venc_, config.venc_channel,
                                    config.vpss_group, config.vpss_channel,
                                    config.main_stream) &&
        (sub_stream_enabled
             ? VencChannelControl::Matches(
                   impl_->sub_venc_, config.sub_venc_channel,
                   config.vpss_group, config.sub_vpss_channel,
                   config.sub_stream)
             : !VencChannelControl::IsCreated(impl_->sub_venc_));
    if (venc_channels_ready) {
        return true;
    }
    if (VencChannelControl::IsCreated(impl_->main_venc_) ||
        VencChannelControl::IsCreated(impl_->sub_venc_)) {
        if (impl_->stream_running_.load() || impl_->stream_thread_.joinable()) {
            Error("hisi_vendor",
                  "reconfigure VENC while stream thread is running");
            return false;
        }
        VencChannelControl::Destroy(impl_->sub_venc_);
        VencChannelControl::Destroy(impl_->main_venc_);
    }

    VencChannelControl::Init(impl_->main_venc_, StreamId::kMain,
                             config.venc_channel, config.vpss_group,
                             config.vpss_channel, config.main_stream.codec);
    if (!VencChannelControl::Create(impl_->main_venc_, config.main_stream)) {
        Error(
            "hisi_vendor",
            "start main VENC failed chn=%d codec=%s rc=%s gop_mode=%s "
            "size=%ux%u src_fps=%d dst_fps=%d bitrate=%u gop=%u",
            config.venc_channel, CodecName(config.main_stream.codec),
            RcModeName(config.main_stream.rc_mode),
            GopModeName(config.main_stream.gop_mode),
            config.main_stream.size.width, config.main_stream.size.height,
            config.main_stream.frame_rate.source_fps,
            config.main_stream.frame_rate.target_fps,
            config.main_stream.bitrate_kbps, config.main_stream.gop);
        VencChannelControl::Reset(impl_->main_venc_);
        return false;
    }

    if (sub_stream_enabled) {
        VencChannelControl::Init(impl_->sub_venc_, StreamId::kSub,
                                 config.sub_venc_channel, config.vpss_group,
                                 config.sub_vpss_channel,
                                 config.sub_stream.codec);
        if (!VencChannelControl::Create(impl_->sub_venc_,
                                        config.sub_stream)) {
            VencChannelControl::Destroy(impl_->main_venc_);
            VencChannelControl::Reset(impl_->sub_venc_);
            Error(
                "hisi_vendor",
                "start sub VENC failed chn=%d codec=%s rc=%s gop_mode=%s "
                "size=%ux%u src_fps=%d dst_fps=%d bitrate=%u gop=%u",
                config.sub_venc_channel, CodecName(config.sub_stream.codec),
                RcModeName(config.sub_stream.rc_mode),
                GopModeName(config.sub_stream.gop_mode),
                config.sub_stream.size.width, config.sub_stream.size.height,
                config.sub_stream.frame_rate.source_fps,
                config.sub_stream.frame_rate.target_fps,
                config.sub_stream.bitrate_kbps, config.sub_stream.gop);
            return false;
        }
    } else {
        VencChannelControl::Reset(impl_->sub_venc_);
    }

    impl_->active_config_ = config;
    impl_->has_active_config_ = true;
    return true;
}

void StopVencStreamThread(MppHisiSdkImpl& impl) {
    if (!impl.stream_running_.load() && !impl.stream_thread_.joinable()) {
        return;
    }

    impl.stream_running_.store(false);

    if (impl.stream_thread_.joinable()) {
        impl.stream_thread_.join();
    }

    impl.frame_callback_ = nullptr;
    impl.frame_callback_user_ = nullptr;
}

void DestroyVencChannels(MppHisiSdkImpl& impl) {
    VencChannelControl::Destroy(impl.sub_venc_);
    VencChannelControl::Destroy(impl.main_venc_);
}

void MppHisiSdk::StopVenc(const MediaPipelineConfig& config) {
    (void)config;
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    StopVencStreamThread(*impl_);
    DestroyVencChannels(*impl_);
}

// ====================================================================
// Bind VPSS → VENC
// ====================================================================
bool MppHisiSdk::BindVpssVenc(const MediaPipelineConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    const bool sub_stream_enabled = config.sub_stream.enabled;
    const bool venc_channels_bound =
        VencChannelControl::IsBoundToVpss(impl_->main_venc_) &&
        (sub_stream_enabled
             ? VencChannelControl::IsBoundToVpss(impl_->sub_venc_)
             : !VencChannelControl::IsBoundToVpss(impl_->sub_venc_));
    if (venc_channels_bound) {
        return true;
    }
    if (!VencChannelControl::IsCreated(impl_->main_venc_) ||
        (sub_stream_enabled &&
         !VencChannelControl::IsCreated(impl_->sub_venc_))) {
        Error("hisi_vendor", "bind VPSS to VENC before VENC is created");
        return false;
    }

    if (!VencChannelControl::BindToVpss(impl_->main_venc_)) {
        Error("hisi_vendor",
              "bind main VPSS to VENC failed vpss=%d:%d venc=%d",
              config.vpss_group, config.vpss_channel,
              config.venc_channel);
        return false;
    }
    if (!VencChannelControl::StartRecv(impl_->main_venc_)) {
        VencChannelControl::UnbindFromVpss(impl_->main_venc_);
        Error("hisi_vendor", "start main VENC recv failed chn=%d",
              config.venc_channel);
        return false;
    }

    if (sub_stream_enabled) {
        if (!VencChannelControl::BindToVpss(impl_->sub_venc_)) {
            VencChannelControl::UnbindFromVpss(impl_->main_venc_);
            Error("hisi_vendor",
                  "bind sub VPSS to VENC failed vpss=%d:%d venc=%d",
                  config.vpss_group, config.sub_vpss_channel,
                  config.sub_venc_channel);
            return false;
        }
        if (!VencChannelControl::StartRecv(impl_->sub_venc_)) {
            VencChannelControl::UnbindFromVpss(impl_->sub_venc_);
            VencChannelControl::UnbindFromVpss(impl_->main_venc_);
            Error("hisi_vendor", "start sub VENC recv failed chn=%d",
                  config.sub_venc_channel);
            return false;
        }
    }

    (void)VencChannelControl::RequestIdr(impl_->main_venc_);
    if (sub_stream_enabled) {
        (void)VencChannelControl::RequestIdr(impl_->sub_venc_);
    }

    return true;
}

void UnbindVpssVencChannels(MppHisiSdkImpl& impl) {
    VencChannelControl::UnbindFromVpss(impl.sub_venc_);
    VencChannelControl::UnbindFromVpss(impl.main_venc_);
}

void MppHisiSdk::UnbindVpssVenc(const MediaPipelineConfig& config) {
    (void)config;
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    StopVencStreamThread(*impl_);
    UnbindVpssVencChannels(*impl_);
}

// ====================================================================
// StartVencStream / StopVencStream
// ====================================================================
bool MppHisiSdk::StartVencStream(const MediaPipelineConfig& config,
                                 MediaFrameCallback callback,
                                 void* user) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    if (impl_->stream_running_.load() || impl_->stream_thread_.joinable()) {
        return true;
    }
    if (!impl_->main_venc_.receiving ||
        (config.sub_stream.enabled && !impl_->sub_venc_.receiving)) {
        Error("hisi_vendor", "start VENC stream before VENC recv is running");
        return false;
    }

    impl_->active_config_ = config;
    impl_->has_active_config_ = true;

    impl_->frame_callback_ = callback;
    impl_->frame_callback_user_ = user;
    impl_->stream_running_.store(true);

    // One stream capture thread monitors all enabled VENC channels.
    impl_->stream_thread_ = std::thread(
        VencStreamCapture::Run, config, callback, user,
        std::ref(impl_->stream_running_));

    return true;
}

void MppHisiSdk::StopVencStream(const MediaPipelineConfig& config) {
    (void)config;
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    StopVencStreamThread(*impl_);
}

// ====================================================================
// RequestIdr
// ====================================================================
bool MppHisiSdk::RequestIdr(int32_t venc_channel) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    if (venc_channel < 0) {
        return false;
    }
    VencChannelInfo* channel = VencChannelControl::Find(
        impl_->main_venc_, impl_->sub_venc_, venc_channel);
    if (channel == nullptr || !channel->created) {
        return false;
    }
    return VencChannelControl::RequestIdr(*channel);
}

bool MppHisiSdk::ApplyVencRoi(int32_t venc_channel,
                              const VideoStreamConfig& stream_config) {
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    VencChannelInfo* channel = VencChannelControl::Find(
        impl_->main_venc_, impl_->sub_venc_, venc_channel);
    if (channel == nullptr || !channel->created) {
        return false;
    }
    if (!ApplyVencRoiConfig(venc_channel, stream_config)) {
        return false;
    }
    channel->stream_config.roi = stream_config.roi;
    return VencChannelControl::RequestIdr(*channel);
}

}  // namespace hisisdk
}  // namespace live_stream
