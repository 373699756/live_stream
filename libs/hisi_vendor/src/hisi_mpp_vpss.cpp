#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

namespace live_stream {
namespace hisisdk {

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
namespace {

bool CheckMpiCall(const char* expression, HI_S32 status) {
    if (status == HI_SUCCESS) {
        return true;
    }
    INFRA_LOG_ERROR("hisi_vendor", "%s failed: 0x%08x", expression, status);
    return false;
}

bool EnableVpssChannel(VPSS_GRP vpss_grp, VPSS_CHN vpss_chn,
                       const VPSS_CHN_ATTR_S& chn_attr) {
    if (!CheckMpiCall("HI_MPI_VPSS_SetChnAttr",
                      HI_MPI_VPSS_SetChnAttr(vpss_grp, vpss_chn, &chn_attr))) {
        return false;
    }
    return CheckMpiCall("HI_MPI_VPSS_EnableChn",
                        HI_MPI_VPSS_EnableChn(vpss_grp, vpss_chn));
}

void CleanupVpssGroup(VPSS_GRP vpss_grp, VPSS_CHN main_chn,
                      bool main_enabled, VPSS_CHN sub_chn,
                      bool sub_enabled) {
    if (sub_enabled) {
        (void)HI_MPI_VPSS_DisableChn(vpss_grp, sub_chn);
    }
    if (main_enabled) {
        (void)HI_MPI_VPSS_DisableChn(vpss_grp, main_chn);
    }
    (void)HI_MPI_VPSS_DestroyGrp(vpss_grp);
}

}  // namespace
#endif  // LIVE_STREAM_ENABLE_HISI_MPP

bool MppHisiSdk::StartVpss(const MediaPipelineConfig& config) {
    if (impl_->vpss_started_) return true;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    // ─── VPSS GROUP attribute ─────────────────────────────────
    VPSS_GRP_ATTR_S grp_attr{};
    grp_attr.u32MaxW = config.main_stream.size.width;
    grp_attr.u32MaxH = config.main_stream.size.height;
    grp_attr.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    grp_attr.enDynamicRange = DYNAMIC_RANGE_SDR8;
    grp_attr.bNrEn = HI_TRUE;
    grp_attr.stFrameRate.s32SrcFrameRate =
        config.main_stream.frame_rate.source_fps;
    grp_attr.stFrameRate.s32DstFrameRate =
        config.main_stream.frame_rate.source_fps;
    VPSS_GRP vpss_grp = static_cast<VPSS_GRP>(config.vpss_group);
    VPSS_CHN vpss_chn = static_cast<VPSS_CHN>(config.vpss_channel);
    VPSS_CHN sub_chn = static_cast<VPSS_CHN>(config.sub_vpss_channel);
    bool main_enabled = false;
    bool sub_enabled = false;

    HISI_CHECK(HI_MPI_VPSS_CreateGrp(vpss_grp, &grp_attr));

    // ─── Main-stream VPSS CHN ─────────────────────────────────
    VPSS_CHN_ATTR_S chn_attr{};
    chn_attr.u32Width = config.main_stream.size.width;
    chn_attr.u32Height = config.main_stream.size.height;
    chn_attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
    chn_attr.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    chn_attr.stFrameRate.s32SrcFrameRate =
        config.main_stream.frame_rate.source_fps;
    chn_attr.stFrameRate.s32DstFrameRate =
        config.main_stream.frame_rate.target_fps;
    chn_attr.enCompressMode = COMPRESS_MODE_NONE;
    chn_attr.u32Depth = 0;
    chn_attr.bMirror = HI_FALSE;
    chn_attr.bFlip = HI_FALSE;

    if (!EnableVpssChannel(vpss_grp, vpss_chn, chn_attr)) {
        CleanupVpssGroup(vpss_grp, vpss_chn, main_enabled, sub_chn, sub_enabled);
        return false;
    }
    main_enabled = true;

    // ─── Sub-stream VPSS CHN (if enabled) ─────────────────────
    if (config.sub_stream.enabled) {
        VPSS_CHN_ATTR_S sub_attr{};
        sub_attr.u32Width = config.sub_stream.size.width;
        sub_attr.u32Height = config.sub_stream.size.height;
        sub_attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
        sub_attr.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        sub_attr.stFrameRate.s32SrcFrameRate =
            config.sub_stream.frame_rate.source_fps;
        sub_attr.stFrameRate.s32DstFrameRate =
            config.sub_stream.frame_rate.target_fps;
        sub_attr.enCompressMode = COMPRESS_MODE_NONE;
        sub_attr.u32Depth = 0;
        sub_attr.bMirror = HI_FALSE;
        sub_attr.bFlip = HI_FALSE;

        if (!EnableVpssChannel(vpss_grp, sub_chn, sub_attr)) {
            CleanupVpssGroup(vpss_grp, vpss_chn, main_enabled, sub_chn, sub_enabled);
            return false;
        }
        sub_enabled = true;
    }

    if (!CheckMpiCall("HI_MPI_VPSS_StartGrp",
                      HI_MPI_VPSS_StartGrp(vpss_grp))) {
        CleanupVpssGroup(vpss_grp, vpss_chn, main_enabled, sub_chn, sub_enabled);
        return false;
    }

    impl_->vpss_started_ = true;
    return true;

#else
    (void)config;
    impl_->vpss_started_ = true;
    return true;
#endif
}

void MppHisiSdk::StopVpss(const MediaPipelineConfig& config) {
    if (!impl_->vpss_started_) return;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    VPSS_GRP vpss_grp = static_cast<VPSS_GRP>(config.vpss_group);

    if (config.sub_stream.enabled) {
        HI_MPI_VPSS_DisableChn(vpss_grp,
                               static_cast<VPSS_CHN>(config.sub_vpss_channel));
    }
    HI_MPI_VPSS_DisableChn(vpss_grp,
                           static_cast<VPSS_CHN>(config.vpss_channel));
    HI_MPI_VPSS_StopGrp(vpss_grp);
    HI_MPI_VPSS_DestroyGrp(vpss_grp);
#else
    (void)config;
#endif

    impl_->vpss_started_ = false;
}

// ─── Bind VI → VPSS ──────────────────────────────────────────
bool MppHisiSdk::BindViVpss(const MediaPipelineConfig& config) {
    if (impl_->vi_bound_vpss_) return true;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    MPP_CHN_S src{};
    src.enModId = HI_ID_VI;
    src.s32DevId = config.video_pipe;
    src.s32ChnId = config.vi_channel;

    MPP_CHN_S dst{};
    dst.enModId = HI_ID_VPSS;
    dst.s32DevId = config.vpss_group;
    dst.s32ChnId = 0;

    HISI_CHECK(HI_MPI_SYS_Bind(&src, &dst));
    impl_->vi_bound_vpss_ = true;
    return true;

#else
    (void)config;
    impl_->vi_bound_vpss_ = true;
    return true;
#endif
}

void MppHisiSdk::UnbindViVpss(const MediaPipelineConfig& config) {
    if (!impl_->vi_bound_vpss_) return;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    MPP_CHN_S src{};
    src.enModId = HI_ID_VI;
    src.s32DevId = config.video_pipe;
    src.s32ChnId = config.vi_channel;

    MPP_CHN_S dst{};
    dst.enModId = HI_ID_VPSS;
    dst.s32DevId = config.vpss_group;
    dst.s32ChnId = 0;

    HI_MPI_SYS_UnBind(&src, &dst);
#else
    (void)config;
#endif

    impl_->vi_bound_vpss_ = false;
}

}  // namespace hisisdk
}  // namespace live_stream
