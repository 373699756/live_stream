#include "hisi_vendor/mpp_hisi_sdk.h"
#include "hisi_mpp_utils.h"
#include "mpp_hisi_sdk_impl.h"

#include <cstring>

namespace live_stream {
namespace hisisdk {

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
namespace {

// ─── Map stream config to MPP picture size ──────────────────────
PIC_SIZE_E PicSizeFromDimensions(uint32_t width, uint32_t height) {
  struct { uint32_t w, h; PIC_SIZE_E ps; } kMap[] = {
    {3840, 2160, PIC_3840x2160},
    {2592, 1520, PIC_2592x1520},
    {2560, 1440, PIC_2560x1440},
    {2048, 1536, PIC_2048x1536},
    {1920, 1080, PIC_1920x1080},
    {1280, 960,  PIC_1280x960},
    {1280, 720,  PIC_1280x720},
    {720,  576,  PIC_720x576},
    {704,  576,  PIC_704x576},
    {640,  480,  PIC_640x480},
    {352,  288,  PIC_352x288},
  };
  for (auto& entry : kMap) {
    if (entry.w == width && entry.h == height) return entry.ps;
  }
  return PIC_1920x1080;
}

// ─── Video buffer configuration ─────────────────────────────────
bool ConfigureVideoBuffer(const MediaPipelineConfig& config) {
  VB_CONF_S vb_conf{};
  (void)std::memset(&vb_conf, 0, sizeof(vb_conf));

  // Main stream VB pool
  vb_conf.astCommPool[0].u32BlkSize =
      config.main_stream.size.width * config.main_stream.size.height * 3 / 2;
  vb_conf.astCommPool[0].u32BlkCnt = config.vb_block_count;
  vb_conf.astCommPool[0].enMmpl = VB_MMC_CACHED;

  // Sub-stream VB pool (if enabled)
  if (config.sub_stream.enabled) {
    vb_conf.astCommPool[1].u32BlkSize =
        config.sub_stream.size.width * config.sub_stream.size.height * 3 / 2;
    vb_conf.astCommPool[1].u32BlkCnt = 4;
    vb_conf.astCommPool[1].enMmpl = VB_MMC_CACHED;
  }

  vb_conf.u32MaxPoolCnt = 4;

  HISI_CHECK(HI_MPI_VB_SetConfig(&vb_conf));
  HISI_CHECK(HI_MPI_VB_Init());

  SYS_VIRUAL_ADDR_UNIFY_CONFIG_S sys_vir_addr_conf{};
  sys_vir_addr_conf.bSupport = HI_FALSE;
  HISI_CHECK(HI_MPI_SYS_SetVirAddrUnifyConfig(&sys_vir_addr_conf));
  HISI_CHECK(HI_MPI_SYS_Init());

  return true;
}

}  // anonymous namespace
#endif  // LIVE_STREAM_ENABLE_HISI_MPP

// ====================================================================
// InitSystem / DeinitSystem
// ====================================================================
bool MppHisiSdk::InitSystem(const MediaPipelineConfig& config) {
  impl_->active_config_ = config;
  impl_->has_active_config_ = true;

  if (impl_->system_initialized_) {
    return true;
  }

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  // 1. Logger for MPP
  HISI_CHECK(HI_MPI_SYS_SetLogLevel(HIS_LOG_LEVEL_ERR));

  // 2. Check chip version
  HI_CHAR version[64]{};
  HISI_CHECK(HI_MPI_SYS_GetVersion(version));
  INFRA_LOG_INFO("hisi_vendor", "HISI MPP version: %s", version);

  // 3. Init Video Buffer + SYS
  if (!ConfigureVideoBuffer(config)) {
    INFRA_LOG_ERROR("hisi_vendor", "ConfigureVideoBuffer failed");
    return false;
  }

  impl_->system_initialized_ = true;
  return true;

#else
  (void)config;
  impl_->system_initialized_ = true;
  return true;
#endif
}

void MppHisiSdk::DeinitSystem() {
  if (!impl_->system_initialized_) {
    return;
  }

  const MediaPipelineConfig& config = impl_->active_config_;

  StopVencStream(config);
  UnbindVpssVenc(config);
  StopVenc(config);
  UnbindViVpss(config);
  StopVpss(config);
  StopVi(config);

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
  HI_MPI_SYS_Exit();
  HI_MPI_VB_Exit();
#endif

  impl_->system_initialized_ = false;
  impl_->has_active_config_ = false;
}

}  // namespace hisisdk
}  // namespace live_stream
