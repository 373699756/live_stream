#include "hisi_sdk_default.h"

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
extern "C" {
#include "mpi_sys.h"
#include "mpi_vb.h"
}
#endif

namespace live_stream {
namespace hisisdk {
namespace {

bool IsValidSize(const VideoSize& size) {
    return size.width > 0 && size.height > 0;
}

#ifdef LIVE_STREAM_ENABLE_HISI_MPP
infra::Status FromHiStatus(int32_t status) {
    return status == HI_SUCCESS ? infra::Status::kOk : infra::Status::kIoError;
}
#endif

}  // namespace

infra::Status DefaultHisiSdk::InitSystem(
    const MediaPipelineConfig& config) {
    if (!IsValidSize(config.main_stream.size) || config.vb_block_count == 0) {
        return infra::Status::kInvalidParam;
    }
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    VB_CONFIG_S vb_config{};
    vb_config.u32MaxPoolCnt = 1;
    vb_config.astCommPool[0].u64BlkSize =
        static_cast<uint64_t>(config.main_stream.size.width) *
        config.main_stream.size.height * 3 / 2;
    vb_config.astCommPool[0].u32BlkCnt = config.vb_block_count;

    HI_MPI_SYS_Exit();
    HI_MPI_VB_Exit();
    int32_t ret = HI_MPI_VB_SetConfig(&vb_config);
    if (ret != HI_SUCCESS) {
        return FromHiStatus(ret);
    }
    ret = HI_MPI_VB_Init();
    if (ret != HI_SUCCESS) {
        return FromHiStatus(ret);
    }
    ret = HI_MPI_SYS_Init();
    if (ret != HI_SUCCESS) {
        HI_MPI_VB_Exit();
        return FromHiStatus(ret);
    }
#endif
    return infra::Status::kOk;
}

void DefaultHisiSdk::DeinitSystem() {
#ifdef LIVE_STREAM_ENABLE_HISI_MPP
    HI_MPI_SYS_Exit();
    HI_MPI_VB_Exit();
#endif
}

}  // namespace hisisdk
}  // namespace live_stream
