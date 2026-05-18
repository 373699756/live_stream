#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <memory>
#include <poll.h>
#include <thread>
#include <utility>
#include <vector>

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

VENC_GOP_ATTR_S GopAttrFromConfig(GopMode mode, uint32_t gop) {
  VENC_GOP_ATTR_S attr{};
  switch (mode) {
    case GopMode::kDualP:
      attr.enGopMode = VENC_GOPMODE_DUALP;
      attr.stDualP.s32IPQpDelta = 4;
      attr.stDualP.s32SPQpDelta = 2;
      attr.stDualP.u32SPInterval = 3;
      break;
    case GopMode::kSmartP:
      attr.enGopMode = VENC_GOPMODE_SMARTP;
      attr.stSmartP.s32BgQpDelta = 4;
      attr.stSmartP.s32ViQpDelta = 2;
      attr.stSmartP.u32BgInterval = gop > 0 ? gop * 3 : 90;
      break;
    case GopMode::kNormalP:
      attr.enGopMode = VENC_GOPMODE_NORMALP;
      attr.stNormalP.s32IPQpDelta = 2;
      break;
  }
  return attr;
}

bool CheckMpiCall(const char* expression, HI_S32 status) {
  if (status == HI_SUCCESS) {
    return true;
  }
  INFRA_LOG_ERROR("hisi_vendor", "%s failed: 0x%08x", expression, status);
  return false;
}

uint32_t VencPackDataLen(const VENC_PACK_S& pack) {
  if (pack.pu8Addr == nullptr || pack.u32Len <= pack.u32Offset) {
    return 0;
  }
  return pack.u32Len - pack.u32Offset;
}

FrameType FrameTypeFromStream(const VENC_STREAM_S& stream, VideoCodec codec) {
  if (codec == VideoCodec::kJpeg || codec == VideoCodec::kMjpeg) {
    return FrameType::kJpeg;
  }

  bool has_i_slice = false;
  bool has_b_slice = false;
  for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
    const VENC_PACK_S& pack = stream.pstPack[i];
    const uint32_t data_num = pack.u32DataNum < 8 ? pack.u32DataNum : 8;
    if (codec == VideoCodec::kH264) {
      if (pack.DataType.enH264EType == H264E_NALU_IDRSLICE) {
        return FrameType::kIdr;
      }
      if (pack.DataType.enH264EType == H264E_NALU_ISLICE) {
        has_i_slice = true;
      } else if (pack.DataType.enH264EType == H264E_NALU_BSLICE) {
        has_b_slice = true;
      }
      for (uint32_t j = 0; j < data_num; ++j) {
        const H264E_NALU_TYPE_E type =
            pack.stPackInfo[j].u32PackType.enH264EType;
        if (type == H264E_NALU_IDRSLICE) {
          return FrameType::kIdr;
        }
        if (type == H264E_NALU_ISLICE) {
          has_i_slice = true;
        } else if (type == H264E_NALU_BSLICE) {
          has_b_slice = true;
        }
      }
    } else if (codec == VideoCodec::kH265) {
      if (pack.DataType.enH265EType == H265E_NALU_IDRSLICE) {
        return FrameType::kIdr;
      }
      if (pack.DataType.enH265EType == H265E_NALU_ISLICE) {
        has_i_slice = true;
      } else if (pack.DataType.enH265EType == H265E_NALU_BSLICE) {
        has_b_slice = true;
      }
      for (uint32_t j = 0; j < data_num; ++j) {
        const H265E_NALU_TYPE_E type =
            pack.stPackInfo[j].u32PackType.enH265EType;
        if (type == H265E_NALU_IDRSLICE) {
          return FrameType::kIdr;
        }
        if (type == H265E_NALU_ISLICE) {
          has_i_slice = true;
        } else if (type == H265E_NALU_BSLICE) {
          has_b_slice = true;
        }
      }
    }
  }

  if (has_i_slice) {
    return FrameType::kI;
  }
  if (has_b_slice) {
    return FrameType::kB;
  }
  return FrameType::kP;
}

bool StartRecvFrame(VENC_CHN venc) {
  VENC_RECV_PIC_PARAM_S recv_param{};
  recv_param.s32RecvPicNum = -1;
  return CheckMpiCall("HI_MPI_VENC_StartRecvFrame",
                      HI_MPI_VENC_StartRecvFrame(venc, &recv_param));
}

void StopRecvFrame(VENC_CHN venc) {
  (void)HI_MPI_VENC_StopRecvFrame(venc);
}

bool BindVpssToVenc(int32_t vpss_group, int32_t vpss_channel,
                    int32_t venc_channel) {
  MPP_CHN_S src{};
  src.enModId = HI_ID_VPSS;
  src.s32DevId = vpss_group;
  src.s32ChnId = vpss_channel;

  MPP_CHN_S dst{};
  dst.enModId = HI_ID_VENC;
  dst.s32DevId = 0;
  dst.s32ChnId = venc_channel;

  return CheckMpiCall("HI_MPI_SYS_Bind(VPSS-VENC)",
                      HI_MPI_SYS_Bind(&src, &dst));
}

void UnbindVpssFromVenc(int32_t vpss_group, int32_t vpss_channel,
                        int32_t venc_channel) {
  MPP_CHN_S src{};
  src.enModId = HI_ID_VPSS;
  src.s32DevId = vpss_group;
  src.s32ChnId = vpss_channel;

  MPP_CHN_S dst{};
  dst.enModId = HI_ID_VENC;
  dst.s32DevId = 0;
  dst.s32ChnId = venc_channel;

  (void)HI_MPI_SYS_UnBind(&src, &dst);
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
  attr.stGopAttr = GopAttrFromConfig(stream.gop_mode, stream.gop);

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

    VENC_CHN_STATUS_S status{};
    HI_S32 s32_ret = HI_MPI_VENC_QueryStatus(venc, &status);
    if (s32_ret != HI_SUCCESS) {
      INFRA_LOG_ERROR("hisi_vendor",
                      "HI_MPI_VENC_QueryStatus chn %d failed: 0x%08x", chn,
                      s32_ret);
      continue;
    }
    if (status.u32CurPacks == 0 || status.u32LeftStreamFrames == 0) {
      continue;
    }

    std::vector<VENC_PACK_S> packs(status.u32CurPacks);
    VENC_STREAM_S stream{};
    stream.pstPack = packs.data();
    stream.u32PackCount = status.u32CurPacks;
    s32_ret = HI_MPI_VENC_GetStream(venc, &stream, 0);
    if (s32_ret != HI_SUCCESS) {
      INFRA_LOG_ERROR("hisi_vendor",
                      "HI_MPI_VENC_GetStream chn %d failed: 0x%08x", chn,
                      s32_ret);
      continue;
    }

    uint64_t total_len = 0;
    for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
      total_len += VencPackDataLen(stream.pstPack[i]);
    }
    if (total_len == 0 ||
        total_len > std::numeric_limits<uint32_t>::max()) {
      HI_MPI_VENC_ReleaseStream(venc, &stream);
      continue;
    }

    auto buffer = std::make_shared<internal::HeapMediaBuffer>(
        static_cast<uint32_t>(total_len));
    uint32_t offset = 0;
    for (uint32_t i = 0; i < stream.u32PackCount; ++i) {
      const VENC_PACK_S& pack = stream.pstPack[i];
      const uint32_t data_len = VencPackDataLen(pack);
      if (data_len == 0) {
        continue;
      }
      std::memcpy(buffer->MutableData() + offset,
                  static_cast<const uint8_t*>(pack.pu8Addr) + pack.u32Offset,
                  data_len);
      offset += data_len;
    }
    buffer->SetSize(offset);

    EncodedFrame frame;
    frame.stream_id = stream_id;
    frame.codec = codec;
    frame.frame_type = FrameTypeFromStream(stream, codec);
    frame.sequence = stream.u32Seq;
    frame.pts_us = stream.pstPack[0].u64PTS;
    frame.dts_us = 0;
    frame.buffer = std::move(buffer);
    frame.offset = 0;
    frame.size = offset;

    if (callback) {
      callback(frame, user);
    }

    HI_MPI_VENC_ReleaseStream(venc, &stream);
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

  // Sub stream (if enabled)
  if (config.sub_stream.enabled) {
    if (!ConfigureVencChannel(config.sub_venc_channel, config.sub_stream)) {
      StopVenc(config);
      return false;
    }
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
  StopRecvFrame(main_venc);
  HI_MPI_VENC_DestroyChn(main_venc);

  if (config.sub_stream.enabled) {
    VENC_CHN sub_venc = static_cast<VENC_CHN>(config.sub_venc_channel);
    StopRecvFrame(sub_venc);
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
  if (!BindVpssToVenc(config.vpss_group, config.vpss_channel,
                      config.venc_channel)) {
    return false;
  }

  // Sub stream (if enabled)
  if (config.sub_stream.enabled) {
    if (!BindVpssToVenc(config.vpss_group, config.sub_vpss_channel,
                        config.sub_venc_channel)) {
      UnbindVpssFromVenc(config.vpss_group, config.vpss_channel,
                         config.venc_channel);
      return false;
    }
  }

  if (!StartRecvFrame(static_cast<VENC_CHN>(config.venc_channel))) {
    if (config.sub_stream.enabled) {
      UnbindVpssFromVenc(config.vpss_group, config.sub_vpss_channel,
                         config.sub_venc_channel);
    }
    UnbindVpssFromVenc(config.vpss_group, config.vpss_channel,
                       config.venc_channel);
    return false;
  }

  if (config.sub_stream.enabled &&
      !StartRecvFrame(static_cast<VENC_CHN>(config.sub_venc_channel))) {
    StopRecvFrame(static_cast<VENC_CHN>(config.venc_channel));
    UnbindVpssFromVenc(config.vpss_group, config.sub_vpss_channel,
                       config.sub_venc_channel);
    UnbindVpssFromVenc(config.vpss_group, config.vpss_channel,
                       config.venc_channel);
    return false;
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
  if (impl_->venc_started_) {
    if (config.sub_stream.enabled) {
      StopRecvFrame(static_cast<VENC_CHN>(config.sub_venc_channel));
    }
    StopRecvFrame(static_cast<VENC_CHN>(config.venc_channel));
  }

  // Main stream unbind
  UnbindVpssFromVenc(config.vpss_group, config.vpss_channel,
                     config.venc_channel);

  if (config.sub_stream.enabled) {
    UnbindVpssFromVenc(config.vpss_group, config.sub_vpss_channel,
                       config.sub_venc_channel);
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
