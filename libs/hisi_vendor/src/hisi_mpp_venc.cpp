#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include <cerrno>
#include <cstring>
#include <memory>
#include <poll.h>
#include <thread>
#include <utility>

namespace live_stream {
namespace hisisdk {

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
namespace {

// ─── Payload type from VideoCodec ──────────────────────────────
PAYLOAD_TYPE_E PayloadFromCodec(VideoCodec codec) {
  switch (codec) {
    case VideoCodec::kH264:   return PT_H264;
    case VideoCodec::kH265:   return PT_H265;
    case VideoCodec::kMjpeg:  return PT_MJPEG;
    case VideoCodec::kJpeg:   return PT_JPEG;
  }
  return PT_H265;
}

// ─── RC mode mapping ───────────────────────────────────────────
VENC_RC_MODE_E RcModeFromConfig(VideoCodec codec, RateControlMode mode) {
  switch (codec) {
    case VideoCodec::kH264:
      switch (mode) {
        case RateControlMode::kCbr:   return VENC_RC_MODE_H264CBR;
        case RateControlMode::kVbr:   return VENC_RC_MODE_H264VBR;
        case RateControlMode::kFixQp: return VENC_RC_MODE_H264FIXQP;
      }
      break;
    case VideoCodec::kH265:
      switch (mode) {
        case RateControlMode::kCbr:   return VENC_RC_MODE_H265CBR;
        case RateControlMode::kVbr:   return VENC_RC_MODE_H265VBR;
        case RateControlMode::kFixQp: return VENC_RC_MODE_H265FIXQP;
      }
      break;
    case VideoCodec::kMjpeg:
      switch (mode) {
        case RateControlMode::kCbr:   return VENC_RC_MODE_MJPEGCBR;
        case RateControlMode::kVbr:   return VENC_RC_MODE_MJPEGVBR;
        case RateControlMode::kFixQp: return VENC_RC_MODE_MJPEGFIXQP;
      }
      break;
    case VideoCodec::kJpeg:
      return VENC_RC_MODE_H264CBR;  // JPEG uses CBR by default
  }
  return VENC_RC_MODE_H264CBR;
}

// ─── Configure a single VENC channel ───────────────────────────
bool ConfigureVencChannel(int32_t chn, const VideoStreamConfig& stream) {
  VENC_CHN venc = static_cast<VENC_CHN>(chn);

  VENC_CHN_ATTR_S attr{};
  attr.stVencAttr.enType = PayloadFromCodec(stream.codec);
  attr.stVencAttr.u32MaxPicWidth = stream.size.width;
  attr.stVencAttr.u32MaxPicHeight = stream.size.height;
  attr.stVencAttr.u32PicWidth = stream.size.width;
  attr.stVencAttr.u32PicHeight = stream.size.height;
  attr.stVencAttr.u32BufSize =
      stream.size.width * stream.size.height * 3 / 2;
  attr.stVencAttr.bByFrame = HI_TRUE;
  attr.stVencAttr.u32Profile = 0;  // default profile

  // RC parameters
  attr.stRcAttr.enRcMode = RcModeFromConfig(stream.codec, stream.rc_mode);
  if (stream.codec == VideoCodec::kH264) {
    if (stream.rc_mode == RateControlMode::kCbr) {
      attr.stRcAttr.stH264Cbr.u32BitRate = stream.bitrate_kbps;
      attr.stRcAttr.stH264Cbr.u32Gop = stream.gop;
      attr.stRcAttr.stH264Cbr.u32SrcFrameRate = stream.frame_rate.source_fps;
      attr.stRcAttr.stH264Cbr.fr32DstFrameRate =
          static_cast<HI_FR32>(stream.frame_rate.target_fps);
    } else if (stream.rc_mode == RateControlMode::kVbr) {
      attr.stRcAttr.stH264Vbr.u32MaxBitRate = stream.bitrate_kbps;
      attr.stRcAttr.stH264Vbr.u32Gop = stream.gop;
      attr.stRcAttr.stH264Vbr.u32SrcFrameRate = stream.frame_rate.source_fps;
      attr.stRcAttr.stH264Vbr.fr32DstFrameRate =
          static_cast<HI_FR32>(stream.frame_rate.target_fps);
    } else {
      attr.stRcAttr.stH264FixQp.u32Gop = stream.gop;
      attr.stRcAttr.stH264FixQp.u32SrcFrameRate = stream.frame_rate.source_fps;
      attr.stRcAttr.stH264FixQp.fr32DstFrameRate =
          static_cast<HI_FR32>(stream.frame_rate.target_fps);
    }
  } else if (stream.codec == VideoCodec::kH265) {
    if (stream.rc_mode == RateControlMode::kCbr) {
      attr.stRcAttr.stH265Cbr.u32BitRate = stream.bitrate_kbps;
      attr.stRcAttr.stH265Cbr.u32Gop = stream.gop;
      attr.stRcAttr.stH265Cbr.u32SrcFrameRate = stream.frame_rate.source_fps;
      attr.stRcAttr.stH265Cbr.fr32DstFrameRate =
          static_cast<HI_FR32>(stream.frame_rate.target_fps);
    } else if (stream.rc_mode == RateControlMode::kVbr) {
      attr.stRcAttr.stH265Vbr.u32MaxBitRate = stream.bitrate_kbps;
      attr.stRcAttr.stH265Vbr.u32Gop = stream.gop;
      attr.stRcAttr.stH265Vbr.u32SrcFrameRate = stream.frame_rate.source_fps;
      attr.stRcAttr.stH265Vbr.fr32DstFrameRate =
          static_cast<HI_FR32>(stream.frame_rate.target_fps);
    } else {
      attr.stRcAttr.stH265FixQp.u32Gop = stream.gop;
      attr.stRcAttr.stH265FixQp.u32SrcFrameRate = stream.frame_rate.source_fps;
      attr.stRcAttr.stH265FixQp.fr32DstFrameRate =
          static_cast<HI_FR32>(stream.frame_rate.target_fps);
    }
  } else if (stream.codec == VideoCodec::kMjpeg) {
    if (stream.rc_mode == RateControlMode::kCbr) {
      attr.stRcAttr.stMjpegCbr.u32BitRate = stream.bitrate_kbps;
      attr.stRcAttr.stMjpegCbr.u32SrcFrameRate = stream.frame_rate.source_fps;
      attr.stRcAttr.stMjpegCbr.fr32DstFrameRate =
          static_cast<HI_FR32>(stream.frame_rate.target_fps);
    } else if (stream.rc_mode == RateControlMode::kVbr) {
      attr.stRcAttr.stMjpegVbr.u32MaxBitRate = stream.bitrate_kbps;
      attr.stRcAttr.stMjpegVbr.u32SrcFrameRate = stream.frame_rate.source_fps;
      attr.stRcAttr.stMjpegVbr.fr32DstFrameRate =
          static_cast<HI_FR32>(stream.frame_rate.target_fps);
    } else {
      attr.stRcAttr.stMjpegFixQp.u32SrcFrameRate = stream.frame_rate.source_fps;
      attr.stRcAttr.stMjpegFixQp.fr32DstFrameRate =
          static_cast<HI_FR32>(stream.frame_rate.target_fps);
    }
  }

  HISI_CHECK(HI_MPI_VENC_CreateChn(venc, &attr));
  return true;
}

// ─── Stream reader thread ──────────────────────────────────────
void VencStreamLoop(int32_t chn, StreamId stream_id, VideoCodec codec,
                    EncodedFrameCallback callback, void* user,
                    std::atomic<bool>* running) {
  VENC_CHN venc = static_cast<VENC_CHN>(chn);
  int fd = HI_MPI_VENC_GetFd(venc);
  if (fd < 0) {
    INFRA_LOG_ERROR("hisi_vendor", "HI_MPI_VENC_GetFd failed for channel %d", chn);
    return;
  }

  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN | POLLERR;

  while (running->load()) {
    int ret = poll(&pfd, 1, 500);
    if (ret < 0) {
      if (errno == EINTR) continue;
      INFRA_LOG_ERROR("hisi_vendor", "poll on VENC %d failed: %s", chn, strerror(errno));
      break;
    }
    if (ret == 0) continue;  // timeout

    if (pfd.revents & POLLERR) {
      INFRA_LOG_ERROR("hisi_vendor", "POLLERR on VENC fd %d", fd);
      break;
    }

    // Query stream
    VENC_STREAM_S stream{};
    stream.pstPack = new VENC_PACK_S[1];  // single pack per query
    HI_S32 s32_ret = HI_MPI_VENC_GetStream(venc, &stream, 0);
    if (s32_ret != HI_SUCCESS) {
      delete[] stream.pstPack;
      continue;
    }

    if (stream.u32PackCount == 0 || stream.pstPack[0].u32Len == 0) {
      HI_MPI_VENC_ReleaseStream(venc, &stream);
      delete[] stream.pstPack;
      continue;
    }

    // Wrap data into an EncodedFrame
    VENC_PACK_S& pack = stream.pstPack[0];
    uint32_t data_len = pack.u32Len - pack.u32Offset;
    auto buffer = std::make_shared<internal::HeapMediaBuffer>(data_len);
    std::memcpy(buffer->MutableData(),
                static_cast<const uint8_t*>(pack.pu8Addr) + pack.u32Offset,
                data_len);
    buffer->SetSize(data_len);

    EncodedFrame frame;
    frame.stream_id = stream_id;
    frame.codec = codec;
    frame.frame_type = (pack.bFrameEnd == HI_TRUE) ? FrameType::kIdr
                                                      : FrameType::kP;
    frame.sequence = 0;  // sequence tracking if needed
    frame.pts_us = pack.u64PTS;
    frame.dts_us = 0;
    frame.buffer = std::move(buffer);
    frame.offset = 0;
    frame.size = data_len;

    if (callback) {
      callback(frame, user);
    }

    HI_MPI_VENC_ReleaseStream(venc, &stream);
    delete[] stream.pstPack;
  }
}

}  // anonymous namespace
#endif  // LIVE_STREAM_ENABLE_HISI_MPP

// ====================================================================
// StartVenc / StopVenc
// ====================================================================
bool MppHisiSdk::StartVenc(const MediaPipelineConfig& config) {
  if (impl_->venc_started_) return true;

  impl_->active_config_ = config;
  impl_->has_active_config_ = true;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  // Main stream
  if (!ConfigureVencChannel(config.venc_channel, config.main_stream)) {
    return false;
  }
  VENC_CHN main_venc = static_cast<VENC_CHN>(config.venc_channel);
  VENC_RECV_PIC_PARAM_S recv_param{};
  recv_param.s32RecvPicNum = -1;  // Continuous receive
  HISI_CHECK(HI_MPI_VENC_StartRecvFrame(main_venc, &recv_param));

  // Sub stream (if enabled)
  if (config.sub_stream.enabled) {
    if (!ConfigureVencChannel(config.sub_venc_channel, config.sub_stream)) {
      StopVenc(config);
      return false;
    }
    VENC_CHN sub_venc = static_cast<VENC_CHN>(config.sub_venc_channel);
    HISI_CHECK(HI_MPI_VENC_StartRecvFrame(sub_venc, &recv_param));
  }

  impl_->venc_started_ = true;
  return true;

#else
  (void)config;
  impl_->venc_started_ = true;
  return true;
#endif
}

void MppHisiSdk::StopVenc(const MediaPipelineConfig& config) {
  if (!impl_->venc_started_) return;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  VENC_CHN main_venc = static_cast<VENC_CHN>(config.venc_channel);
  HI_MPI_VENC_StopRecvFrame(main_venc);
  HI_MPI_VENC_DestroyChn(main_venc);

  if (config.sub_stream.enabled) {
    VENC_CHN sub_venc = static_cast<VENC_CHN>(config.sub_venc_channel);
    HI_MPI_VENC_StopRecvFrame(sub_venc);
    HI_MPI_VENC_DestroyChn(sub_venc);
  }
#else
  (void)config;
#endif

  impl_->venc_started_ = false;
}

// ====================================================================
// Bind VPSS → VENC
// ====================================================================
bool MppHisiSdk::BindVpssVenc(const MediaPipelineConfig& config) {
  if (impl_->vpss_bound_venc_) return true;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  // Main stream: VPSS CHN → VENC
  {
    MPP_CHN_S src{};
    src.enModId = HI_ID_VPSS;
    src.s32DevId = config.vpss_group;
    src.s32ChnId = config.vpss_channel;

    MPP_CHN_S dst{};
    dst.enModId = HI_ID_VENC;
    dst.s32DevId = 0;
    dst.s32ChnId = config.venc_channel;

    HISI_CHECK(HI_MPI_SYS_Bind(&src, &dst));
  }

  // Sub stream (if enabled)
  if (config.sub_stream.enabled) {
    MPP_CHN_S src{};
    src.enModId = HI_ID_VPSS;
    src.s32DevId = config.vpss_group;
    src.s32ChnId = config.sub_vpss_channel;

    MPP_CHN_S dst{};
    dst.enModId = HI_ID_VENC;
    dst.s32DevId = 0;
    dst.s32ChnId = config.sub_venc_channel;

    HISI_CHECK(HI_MPI_SYS_Bind(&src, &dst));
  }

  impl_->vpss_bound_venc_ = true;
  return true;

#else
  (void)config;
  impl_->vpss_bound_venc_ = true;
  return true;
#endif
}

void MppHisiSdk::UnbindVpssVenc(const MediaPipelineConfig& config) {
  if (!impl_->vpss_bound_venc_) return;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  // Main stream unbind
  {
    MPP_CHN_S src{};
    src.enModId = HI_ID_VPSS;
    src.s32DevId = config.vpss_group;
    src.s32ChnId = config.vpss_channel;

    MPP_CHN_S dst{};
    dst.enModId = HI_ID_VENC;
    dst.s32DevId = 0;
    dst.s32ChnId = config.venc_channel;

    HI_MPI_SYS_UnBind(&src, &dst);
  }

  if (config.sub_stream.enabled) {
    MPP_CHN_S src{};
    src.enModId = HI_ID_VPSS;
    src.s32DevId = config.vpss_group;
    src.s32ChnId = config.sub_vpss_channel;

    MPP_CHN_S dst{};
    dst.enModId = HI_ID_VENC;
    dst.s32DevId = 0;
    dst.s32ChnId = config.sub_venc_channel;

    HI_MPI_SYS_UnBind(&src, &dst);
  }
#else
  (void)config;
#endif

  impl_->vpss_bound_venc_ = false;
}

// ====================================================================
// StartVencStream / StopVencStream
// ====================================================================
bool MppHisiSdk::StartVencStream(const MediaPipelineConfig& config,
                                 EncodedFrameCallback callback,
                                 void* user) {
  if (impl_->stream_started_) return true;

  impl_->active_config_ = config;
  impl_->has_active_config_ = true;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  impl_->frame_callback_ = callback;
  impl_->frame_callback_user_ = user;
  impl_->stream_running_.store(true);

  // Main stream thread
  impl_->main_stream_thread_ = std::thread(
      VencStreamLoop, config.venc_channel, StreamId::kMain,
      config.main_stream.codec, callback, user, &impl_->stream_running_);

  // Sub stream thread (if enabled)
  if (config.sub_stream.enabled) {
    impl_->sub_stream_thread_ = std::thread(
        VencStreamLoop, config.sub_venc_channel, StreamId::kSub,
        config.sub_stream.codec, callback, user, &impl_->stream_running_);
  }

  impl_->stream_started_ = true;
  return true;

#else
  (void)config;
  (void)callback;
  (void)user;
  impl_->stream_started_ = true;
  return true;
#endif
}

void MppHisiSdk::StopVencStream(const MediaPipelineConfig& config) {
  if (!impl_->stream_started_) return;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  // Stop uses the same configured channel set as StartVencStream.
  (void)config;
#else
  (void)config;
#endif

  impl_->stream_running_.store(false);

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  if (impl_->main_stream_thread_.joinable()) {
    impl_->main_stream_thread_.join();
  }
  if (impl_->sub_stream_thread_.joinable()) {
    impl_->sub_stream_thread_.join();
  }
#endif

  impl_->frame_callback_ = nullptr;
  impl_->frame_callback_user_ = nullptr;
  impl_->stream_started_ = false;
}

// ====================================================================
// RequestIdr
// ====================================================================
bool MppHisiSdk::RequestIdr(int32_t venc_channel) {
  if (venc_channel < 0) return false;

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  return internal::HiOk(HI_MPI_VENC_RequestIDR(
      static_cast<VENC_CHN>(venc_channel), HI_TRUE));
#else
  (void)venc_channel;
  return true;
#endif
}

}  // namespace hisisdk
}  // namespace live_stream
