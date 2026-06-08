#ifndef LIVE_STREAM_AI_SRC_HISI_AI_PLATFORM_H_
#define LIVE_STREAM_AI_SRC_HISI_AI_PLATFORM_H_

#include <cstdint>
#include <limits>

#if defined(LIVE_STREAM_ENABLE_HISI_MPP) &&                      \
    defined(LIVE_STREAM_ENABLE_HISI_NNIE) &&                     \
    __has_include("mpi_nnie.h") && __has_include("mpi_sys.h") && \
                                                 __has_include("ivs_md.h")
#define LIVE_STREAM_HAS_HISI_NNIE 1
extern "C" {
#include "hi_comm_vb.h"
#include "hi_comm_video.h"
#include "ivs_md.h"
#include "mpi_ive.h"
#include "mpi_nnie.h"
#include "mpi_sys.h"
#include "mpi_vgs.h"
}
#else
#define LIVE_STREAM_HAS_HISI_NNIE 0
#endif

namespace live_stream {
namespace ai_internal {

#if LIVE_STREAM_HAS_HISI_NNIE
constexpr uint64_t kMaxHiU32 = std::numeric_limits<HI_U32>::max();

inline HI_VOID *VirAddrToPointer(HI_U64 vir_addr) {
    return reinterpret_cast<HI_VOID *>(static_cast<HI_UL>(vir_addr));
}

inline bool QueryIveTask(IVE_HANDLE handle) {
    HI_BOOL finished = HI_FALSE;
    const HI_S32 ret = HI_MPI_IVE_Query(handle, &finished, HI_TRUE);
    return ret == HI_SUCCESS && finished == HI_TRUE;
}
#endif

inline uint32_t AlignUpU32(uint32_t value, uint32_t alignment) {
    if (alignment == 0) {
        return value;
    }
    return ((value + alignment - 1U) / alignment) * alignment;
}

inline float ClampFloat(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_HISI_AI_PLATFORM_H_
