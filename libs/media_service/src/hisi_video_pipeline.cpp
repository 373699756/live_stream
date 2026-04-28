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
infra::Status FromHiStatus(int32_t status) {
    return status == HI_SUCCESS ? infra::Status::kOk : infra::Status::kIoError;
}
#endif

}  // namespace

infra::Status DefaultHisiSdk::StartVi(const MediaPipelineConfig& config) {
    return config.video_pipe >= 0 ? infra::Status::kOk
                                  : infra::Status::kInvalidParam;
}

void DefaultHisiSdk::StopVi(const MediaPipelineConfig& config) {
    (void)config;
}

infra::Status DefaultHisiSdk::StartVpss(const MediaPipelineConfig& config) {
    return config.vpss_group >= 0 ? infra::Status::kOk
                                  : infra::Status::kInvalidParam;
}

void DefaultHisiSdk::StopVpss(const MediaPipelineConfig& config) {
    (void)config;
}

infra::Status DefaultHisiSdk::BindViVpss(const MediaPipelineConfig& config) {
    return config.vi_channel >= 0 ? infra::Status::kOk
                                  : infra::Status::kInvalidParam;
}

void DefaultHisiSdk::UnbindViVpss(const MediaPipelineConfig& config) {
    (void)config;
}

infra::Status DefaultHisiSdk::StartVenc(const MediaPipelineConfig& config) {
    if (config.venc_channel < 0) {
        return infra::Status::kInvalidParam;
    }
    // Board-specific VENC attribute setup belongs here in real MPP builds.
    return infra::Status::kOk;
}

void DefaultHisiSdk::StopVenc(const MediaPipelineConfig& config) {
    (void)config;
}

infra::Status DefaultHisiSdk::BindVpssVenc(
    const MediaPipelineConfig& config) {
    return config.vpss_channel >= 0 ? infra::Status::kOk
                                    : infra::Status::kInvalidParam;
}

void DefaultHisiSdk::UnbindVpssVenc(const MediaPipelineConfig& config) {
    (void)config;
}

infra::Status DefaultHisiSdk::StartVencStream(
    const MediaPipelineConfig& config,
    EncodedFrameCallback callback,
    void* user) {
    (void)callback;
    (void)user;
    return config.main_stream.bitrate_kbps > 0 ? infra::Status::kOk
                                               : infra::Status::kInvalidParam;
}

void DefaultHisiSdk::StopVencStream(const MediaPipelineConfig& config) {
    (void)config;
}

infra::Status DefaultHisiSdk::RequestIdr(int32_t venc_channel) {
    if (venc_channel < 0) {
        return infra::Status::kInvalidParam;
    }
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    return FromHiStatus(HI_MPI_VENC_RequestIDR(venc_channel, HI_TRUE));
#else
    return infra::Status::kOk;
#endif
}

}  // namespace hisisdk
}  // namespace live_stream
