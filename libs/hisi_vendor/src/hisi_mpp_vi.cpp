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

VI_PIPE_ATTR_S MakePipeAttr(const MediaPipelineConfig& config) {
  VI_PIPE_ATTR_S pipe_attr{};
  pipe_attr.enPipeBypassMode = VI_PIPE_BYPASS_NONE;
  pipe_attr.bYuvSkip = HI_FALSE;
  pipe_attr.bIspBypass = HI_FALSE;
  pipe_attr.u32MaxW = config.main_stream.size.width;
  pipe_attr.u32MaxH = config.main_stream.size.height;
  pipe_attr.enPixFmt = PIXEL_FORMAT_RGB_BAYER_12BPP;
  pipe_attr.enCompressMode = COMPRESS_MODE_LINE;
  pipe_attr.enBitWidth = DATA_BITWIDTH_12;
  pipe_attr.bNrEn = HI_FALSE;
  pipe_attr.stNrAttr.enPixFmt = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
  pipe_attr.stNrAttr.enBitWidth = DATA_BITWIDTH_8;
  pipe_attr.stNrAttr.enNrRefSource = VI_NR_REF_FROM_RFR;
  pipe_attr.stNrAttr.enCompressMode = COMPRESS_MODE_NONE;
  pipe_attr.bSharpenEn = HI_FALSE;
  pipe_attr.stFrameRate.s32SrcFrameRate =
      config.main_stream.frame_rate.source_fps;
  pipe_attr.stFrameRate.s32DstFrameRate =
      config.main_stream.frame_rate.source_fps;
  pipe_attr.bDiscardProPic = HI_FALSE;
  return pipe_attr;
}

bool BindDevPipe(VI_DEV vi_dev, VI_PIPE vi_pipe) {
  VI_DEV_BIND_PIPE_S bind_pipe{};
  bind_pipe.u32Num = 1;
  bind_pipe.PipeId[0] = vi_pipe;
  return CheckMpiCall("HI_MPI_VI_SetDevBindPipe",
                      HI_MPI_VI_SetDevBindPipe(vi_dev, &bind_pipe));
}

void StopPipe(VI_PIPE vi_pipe) {
  (void)HI_MPI_VI_StopPipe(vi_pipe);
  (void)HI_MPI_VI_DestroyPipe(vi_pipe);
}

}  // namespace
#endif  // LIVE_STREAM_ENABLE_HISI_MPP

bool MppHisiSdk::StartVi(const MediaPipelineConfig& config) {
  if (impl_->vi_started_) return true;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  // ─── VI DEV attribute ─────────────────────────────────────
  VI_DEV_ATTR_S dev_attr{};
  (void)std::memset(&dev_attr, 0, sizeof(dev_attr));
  // Use sample_comm defaults; on Hi3516CV500 with MIPI sensor:
  dev_attr.enIntfMode = VI_MODE_MIPI;
  dev_attr.enDataSeq = VI_DATA_SEQ_YUYV;
  dev_attr.enWorkMode = VI_WORK_MODE_1Multiplex;
  dev_attr.enInputDataType = VI_DATA_TYPE_RGB;
  dev_attr.stSize.u32Width = config.main_stream.size.width;
  dev_attr.stSize.u32Height = config.main_stream.size.height;
  dev_attr.enScanMode = VI_SCAN_PROGRESSIVE;
  dev_attr.au32ComponentMask[0] = 0xFFF00000;
  dev_attr.au32ComponentMask[1] = 0;
  dev_attr.as32AdChnId[0] = -1;
  dev_attr.as32AdChnId[1] = -1;
  dev_attr.as32AdChnId[2] = -1;
  dev_attr.as32AdChnId[3] = -1;
  dev_attr.bDataReverse = HI_FALSE;
  dev_attr.stWDRAttr.enWDRMode = WDR_MODE_NONE;
  dev_attr.stWDRAttr.u32CacheLine = config.main_stream.size.height;
  dev_attr.enDataRate = DATA_RATE_X1;

  VI_DEV vi_dev = static_cast<VI_DEV>(config.sensor_id);
  HISI_CHECK(HI_MPI_VI_SetDevAttr(vi_dev, &dev_attr));
  HISI_CHECK(HI_MPI_VI_EnableDev(vi_dev));

  VI_PIPE vi_pipe = static_cast<VI_PIPE>(config.video_pipe);
  if (!BindDevPipe(vi_dev, vi_pipe)) {
    (void)HI_MPI_VI_DisableDev(vi_dev);
    return false;
  }

  VI_PIPE_ATTR_S pipe_attr = MakePipeAttr(config);
  if (!CheckMpiCall("HI_MPI_VI_CreatePipe",
                    HI_MPI_VI_CreatePipe(vi_pipe, &pipe_attr))) {
    (void)HI_MPI_VI_DisableDev(vi_dev);
    return false;
  }
  if (!CheckMpiCall("HI_MPI_VI_StartPipe",
                    HI_MPI_VI_StartPipe(vi_pipe))) {
    (void)HI_MPI_VI_DestroyPipe(vi_pipe);
    (void)HI_MPI_VI_DisableDev(vi_dev);
    return false;
  }

  // ─── VI CHN attribute ─────────────────────────────────────
  VI_CHN_ATTR_S chn_attr{};
  chn_attr.stSize.u32Width = config.main_stream.size.width;
  chn_attr.stSize.u32Height = config.main_stream.size.height;
  chn_attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
  chn_attr.enCompressMode = COMPRESS_MODE_NONE;
  chn_attr.u32Depth = 0;
  chn_attr.enDynamicRange = DYNAMIC_RANGE_SDR8;
  chn_attr.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;

  VI_CHN vi_chn = static_cast<VI_CHN>(config.vi_channel);
  if (!CheckMpiCall("HI_MPI_VI_SetChnAttr",
                    HI_MPI_VI_SetChnAttr(vi_pipe, vi_chn, &chn_attr))) {
    StopPipe(vi_pipe);
    (void)HI_MPI_VI_DisableDev(vi_dev);
    return false;
  }
  if (!CheckMpiCall("HI_MPI_VI_EnableChn",
                    HI_MPI_VI_EnableChn(vi_pipe, vi_chn))) {
    StopPipe(vi_pipe);
    (void)HI_MPI_VI_DisableDev(vi_dev);
    return false;
  }

  impl_->vi_started_ = true;
  return true;

#else
  (void)config;
  impl_->vi_started_ = true;
  return true;
#endif
}

void MppHisiSdk::StopVi(const MediaPipelineConfig& config) {
  if (!impl_->vi_started_) return;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  VI_PIPE vi_pipe = static_cast<VI_PIPE>(config.video_pipe);
  VI_CHN vi_chn = static_cast<VI_CHN>(config.vi_channel);
  HI_MPI_VI_DisableChn(vi_pipe, vi_chn);
  StopPipe(vi_pipe);
  HI_MPI_VI_DisableDev(static_cast<VI_DEV>(config.sensor_id));
#else
  (void)config;
#endif

  impl_->vi_started_ = false;
}

}  // namespace hisisdk
}  // namespace live_stream
