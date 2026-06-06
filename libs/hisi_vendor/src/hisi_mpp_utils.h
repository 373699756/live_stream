#ifndef LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_UTILS_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_UTILS_H_

#include <cstdint>

#include "infra/log.h"

extern "C" {
#include "hi_common.h"
#include "hi_defines.h"
#include "hi_comm_sys.h"
#include "hi_comm_vb.h"
#include "hi_comm_vi.h"
#include "hi_comm_vpss.h"
#include "hi_comm_venc.h"
#include "hi_comm_region.h"
#include "mpi_vb.h"
#include "mpi_sys.h"
#include "mpi_vi.h"
#include "mpi_vpss.h"
#include "mpi_venc.h"
#include "mpi_region.h"
#include "mpi_isp.h"
#include "mpi_ae.h"
#include "mpi_awb.h"
#include "hi_ae_comm.h"
#include "hi_awb_comm.h"
#include "hi_mipi.h"
#include "hi_sns_ctrl.h"
}

namespace live_stream {
namespace hisisdk {

inline bool MpiOk(const char* expression, HI_S32 status) {
    if (status == HI_SUCCESS) {
        return true;
    }
    Error("hisi_vendor", "%s failed: 0x%08x", expression, status);
    return false;
}

namespace internal {

// ------------------------------------------------------------------
// Helper macros
// ------------------------------------------------------------------

// Check HiSilicon SDK return code, log on failure, return false.
#define HISI_CHECK(expr)                                                       \
    do {                                                                       \
        HI_S32 __ret = (expr);                                                 \
        if (__ret != HI_SUCCESS) {                                             \
            Error("hisi_vendor", "%s failed: 0x%08x", #expr, __ret); \
            return false;                                                      \
        }                                                                      \
    } while (0)

// Single-expression version for use in ternary / assignment.
inline bool HiOk(HI_S32 status) { return status == HI_SUCCESS; }

struct VencPacketSpan {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

struct VencPacketData {
    VencPacketSpan first;
    VencPacketSpan second;
    uint32_t size = 0;
};

inline bool FindVencStreamBuffer(const VENC_STREAM_BUF_INFO_S& stream_buffer,
                                 uintptr_t packet_addr,
                                 uintptr_t* base,
                                 uintptr_t* end) {
    if (base == nullptr || end == nullptr) {
        return false;
    }
    for (uint32_t i = 0; i < MAX_TILE_NUM; ++i) {
        const uintptr_t tile_base =
            reinterpret_cast<uintptr_t>(stream_buffer.pUserAddr[i]);
        const uint64_t tile_size = stream_buffer.u64BufSize[i];
        const uintptr_t tile_size_ptr = static_cast<uintptr_t>(tile_size);
        if (tile_base == 0 || tile_size == 0 ||
            static_cast<uint64_t>(tile_size_ptr) != tile_size ||
            tile_base > UINTPTR_MAX - tile_size_ptr) {
            continue;
        }
        const uintptr_t tile_end = tile_base + tile_size_ptr;
        if (packet_addr >= tile_base && packet_addr < tile_end) {
            *base = tile_base;
            *end = tile_end;
            return true;
        }
    }
    return false;
}

inline bool GetVencPacketData(const VENC_PACK_S& pack,
                              const VENC_STREAM_BUF_INFO_S& stream_buffer,
                              VencPacketData* data) {
    if (data == nullptr || pack.pu8Addr == nullptr ||
        pack.u32Offset > pack.u32Len) {
        return false;
    }
    *data = VencPacketData{};
    data->size = pack.u32Len - pack.u32Offset;
    if (data->size == 0) {
        return true;
    }

    const uintptr_t packet_addr =
        reinterpret_cast<uintptr_t>(pack.pu8Addr) + pack.u32Offset;
    data->first.data = reinterpret_cast<const uint8_t*>(packet_addr);
    data->first.size = data->size;

    uintptr_t base = 0;
    uintptr_t end = 0;
    if (!FindVencStreamBuffer(stream_buffer, packet_addr, &base, &end)) {
        return true;
    }

    const uintptr_t tail_size = end - packet_addr;
    if (static_cast<uint64_t>(data->size) >
        static_cast<uint64_t>(tail_size)) {
        data->first.size = static_cast<uint32_t>(tail_size);
        data->second.data = reinterpret_cast<const uint8_t*>(base);
        data->second.size = data->size - data->first.size;
    }
    return true;
}

}  // namespace internal
}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_UTILS_H_
