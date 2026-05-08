#include "hisi_sdk_default.h"

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
extern "C" {
#include "hi_comm_venc.h"
#include "mpi_venc.h"
}
#endif

namespace live_stream {
namespace hisisdk {
namespace {

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
bool FromHiStatus(int32_t status) { return status == HI_SUCCESS; }
#endif

}  // namespace

bool DefaultHisiSdk::StartVi(const MediaPipelineConfig& config) {
    return config.video_pipe >= 0;
}

void DefaultHisiSdk::StopVi(const MediaPipelineConfig& config) {
    (void)config;
}

bool DefaultHisiSdk::StartVpss(const MediaPipelineConfig& config) {
    return config.vpss_group >= 0;
}

void DefaultHisiSdk::StopVpss(const MediaPipelineConfig& config) {
    (void)config;
}

bool DefaultHisiSdk::BindViVpss(const MediaPipelineConfig& config) {
    return config.vi_channel >= 0;
}

void DefaultHisiSdk::UnbindViVpss(const MediaPipelineConfig& config) {
    (void)config;
}

bool DefaultHisiSdk::StartVenc(const MediaPipelineConfig& config) {
    if (config.venc_channel < 0) {
        return false;
    }
    if (config.sub_stream.enabled && config.sub_venc_channel < 0) {
        return false;
    }
    // Board-specific VENC attribute setup belongs here in real MPP builds.
    return true;
}

void DefaultHisiSdk::StopVenc(const MediaPipelineConfig& config) {
    (void)config;
}

bool DefaultHisiSdk::BindVpssVenc(const MediaPipelineConfig& config) {
    if (config.vpss_channel < 0) {
        return false;
    }
    return !config.sub_stream.enabled || config.sub_vpss_channel >= 0;
}

void DefaultHisiSdk::UnbindVpssVenc(const MediaPipelineConfig& config) {
    (void)config;
}

bool DefaultHisiSdk::StartVencStream(
    const MediaPipelineConfig& config,
    EncodedFrameCallback callback,
    void* user) {
    (void)callback;
    (void)user;
    return config.main_stream.bitrate_kbps > 0 &&
           (!config.sub_stream.enabled || config.sub_stream.bitrate_kbps > 0);
}

void DefaultHisiSdk::StopVencStream(const MediaPipelineConfig& config) {
    (void)config;
}

bool DefaultHisiSdk::RequestIdr(int32_t venc_channel) {
    if (venc_channel < 0) {
        return false;
    }
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    return FromHiStatus(HI_MPI_VENC_RequestIDR(venc_channel, HI_TRUE));
#else
    return true;
#endif
}

}  // namespace hisisdk
}  // namespace live_stream
