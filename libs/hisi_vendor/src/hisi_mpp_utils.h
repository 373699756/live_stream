#ifndef LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_UTILS_H_
#define LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_UTILS_H_

#include <cstdint>
#include <cstring>
#include <vector>

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
namespace internal {

// ------------------------------------------------------------------
// Helper macros
// ------------------------------------------------------------------

// Check HiSilicon SDK return code, log on failure, return false.
#define HISI_CHECK(expr)                                                       \
    do {                                                                       \
        HI_S32 __ret = (expr);                                                 \
        if (__ret != HI_SUCCESS) {                                             \
            INFRA_LOG_ERROR("hisi_vendor", "%s failed: 0x%08x", #expr, __ret); \
            return false;                                                      \
        }                                                                      \
    } while (0)

// Check HiSilicon SDK return code, log on failure, propagate error code.
#define HISI_CHECK_ERR(expr)                                                   \
    do {                                                                       \
        HI_S32 __ret = (expr);                                                 \
        if (__ret != HI_SUCCESS) {                                             \
            INFRA_LOG_ERROR("hisi_vendor", "%s failed: 0x%08x", #expr, __ret); \
            return __ret;                                                      \
        }                                                                      \
    } while (0)

// Single-expression version for use in ternary / assignment.
inline bool HiOk(HI_S32 status) { return status == HI_SUCCESS; }

// ------------------------------------------------------------------
// Common helpers (required by all modules)
// ------------------------------------------------------------------

// Trivial IMediaBuffer implementation backed by a heap-allocated vector.
class HeapMediaBuffer final : public IMediaBuffer {
public:
    explicit HeapMediaBuffer(uint32_t capacity) : data_(capacity) {}

    uint8_t* MutableData() override { return data_.data(); }
    const uint8_t* Data() const override { return data_.data(); }
    uint32_t Size() const override {
        return static_cast<uint32_t>(data_.size());
    }
    uint32_t Capacity() const override {
        return static_cast<uint32_t>(data_.capacity());
    }
    bool SetSize(uint32_t size) override {
        if (size <= data_.size()) {
            data_.resize(size);
            return true;
        }
        return false;
    }

private:
    std::vector<uint8_t> data_;
};

}  // namespace internal
}  // namespace hisisdk
}  // namespace live_stream

#endif  // LIVE_STREAM_HISI_VENDOR_SRC_HISI_MPP_UTILS_H_
