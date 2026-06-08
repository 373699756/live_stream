#include "ai_engine.h"

#include "ai_config.h"
#include "infra/fs.h"
#include "infra/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

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
namespace {

#if LIVE_STREAM_HAS_HISI_NNIE
constexpr HI_U32 kNnieMaxInputNum = 1;
constexpr HI_U32 kNnieAlign16 = 16;
constexpr uint64_t kMaxHiU32 = std::numeric_limits<HI_U32>::max();
constexpr uint32_t kSsdLayerCount = 6;
constexpr uint32_t kSsdReportNodeCount = 12;
constexpr uint32_t kSsdClassCount = 21;
constexpr uint32_t kSsdInputWidth = 300;
constexpr uint32_t kSsdInputHeight = 300;
constexpr uint32_t kSsdPriorCount = 8732;
constexpr uint32_t kSsdCoordinateCount = 4;
constexpr uint32_t kSsdTopK = 400;
constexpr uint32_t kSsdKeepTopK = 200;
constexpr int32_t kSsdQuantBase = 4096;
constexpr float kSsdNmsThreshold = 0.3f;
constexpr uint32_t kVgsFrameAlign = 32;
constexpr uint32_t kIveImageAlign = 16;
constexpr uint32_t kIveCscMinWidth = 64;
constexpr uint32_t kIveCscMinHeight = 64;
constexpr uint32_t kMotionImageCount = 2;
constexpr uint32_t kMotionAreaThresholdStep = 8;
constexpr HI_U16 kMotionSadThreshold = 200;
constexpr HI_U0Q16 kMotionBackgroundBlend = 32768;
constexpr MD_CHN kMotionChannel = 0;
constexpr uint32_t kOcclusionGridWidth = 8;
constexpr uint32_t kOcclusionGridHeight = 8;
constexpr uint32_t kOcclusionGridCount =
    kOcclusionGridWidth * kOcclusionGridHeight;
constexpr uint32_t kOcclusionHitThreshold = kOcclusionGridCount / 2U;
constexpr int32_t kOcclusionLineMean0 = 80;
constexpr int32_t kOcclusionLineSigma0 = 0;
constexpr int32_t kOcclusionLineMean1 = 80;
constexpr int32_t kOcclusionLineSigma1 = 20;
constexpr HI_U64 kOcclusionIntegSumMask = 0x0fffffffULL;
constexpr uint32_t kOcclusionIntegSquareShift = 28;

constexpr std::array<uint32_t, kSsdLayerCount> kSsdPriorBoxWidth = {
    {38, 19, 10, 5, 3, 1}};
constexpr std::array<uint32_t, kSsdLayerCount> kSsdPriorBoxHeight = {
    {38, 19, 10, 5, 3, 1}};
constexpr std::array<float, kSsdLayerCount> kSsdPriorMinSize = {
    {30.0f, 60.0f, 111.0f, 162.0f, 213.0f, 264.0f}};
constexpr std::array<float, kSsdLayerCount> kSsdPriorMaxSize = {
    {60.0f, 111.0f, 162.0f, 213.0f, 264.0f, 315.0f}};
constexpr std::array<uint32_t, kSsdLayerCount> kSsdAspectRatioCount = {
    {1, 2, 2, 2, 1, 1}};
constexpr std::array<std::array<float, 2>, kSsdLayerCount>
    kSsdAspectRatios = {{{{2.0f, 0.0f}},
                         {{2.0f, 3.0f}},
                         {{2.0f, 3.0f}},
                         {{2.0f, 3.0f}},
                         {{2.0f, 0.0f}},
                         {{2.0f, 0.0f}}}};
constexpr std::array<float, kSsdLayerCount> kSsdPriorStepWidth = {
    {8.0f, 16.0f, 32.0f, 64.0f, 100.0f, 300.0f}};
constexpr std::array<float, kSsdLayerCount> kSsdPriorStepHeight = {
    {8.0f, 16.0f, 32.0f, 64.0f, 100.0f, 300.0f}};
constexpr std::array<int32_t, kSsdCoordinateCount> kSsdPriorVariance = {
    {409, 409, 819, 819}};
constexpr std::array<uint32_t, kSsdLayerCount> kSsdSoftmaxInputChannel = {
    {121296, 45486, 12600, 3150, 756, 84}};
constexpr std::array<uint32_t, kSsdLayerCount> kSsdDetectInputChannel = {
    {23104, 8664, 2400, 600, 144, 16}};
constexpr std::array<const char *, kSsdClassCount> kSsdVocLabels = {
    {"background", "aeroplane", "bicycle", "bird", "boat", "bottle", "bus",
     "car", "cat", "chair", "cow", "diningtable", "dog", "horse",
     "motorbike", "person", "pottedplant", "sheep", "sofa", "train",
     "tvmonitor"}};

std::array<int, 256> BuildYuvYTable() {
    std::array<int, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        const int c = static_cast<int>(i) > 16 ? static_cast<int>(i) - 16 : 0;
        table[i] = 298 * c;
    }
    return table;
}

std::array<int, 256> BuildYuvUToBTable() {
    std::array<int, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        table[i] = 516 * (static_cast<int>(i) - 128);
    }
    return table;
}

std::array<int, 256> BuildYuvUToGTable() {
    std::array<int, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        table[i] = -100 * (static_cast<int>(i) - 128);
    }
    return table;
}

std::array<int, 256> BuildYuvVToRTable() {
    std::array<int, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        table[i] = 409 * (static_cast<int>(i) - 128);
    }
    return table;
}

std::array<int, 256> BuildYuvVToGTable() {
    std::array<int, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        table[i] = -208 * (static_cast<int>(i) - 128);
    }
    return table;
}

const std::array<int, 256> kYuvYTable = BuildYuvYTable();
const std::array<int, 256> kYuvUToBTable = BuildYuvUToBTable();
const std::array<int, 256> kYuvUToGTable = BuildYuvUToGTable();
const std::array<int, 256> kYuvVToRTable = BuildYuvVToRTable();
const std::array<int, 256> kYuvVToGTable = BuildYuvVToGTable();

struct NnieSegData {
    SVP_SRC_BLOB_S src[SVP_NNIE_MAX_INPUT_NUM];
    SVP_DST_BLOB_S dst[SVP_NNIE_MAX_OUTPUT_NUM];
};

struct NnieBlobSize {
    HI_U32 src[SVP_NNIE_MAX_INPUT_NUM];
    HI_U32 dst[SVP_NNIE_MAX_OUTPUT_NUM];
};

struct SsdPrior {
    float x_min = 0.0f;
    float y_min = 0.0f;
    float x_max = 0.0f;
    float y_max = 0.0f;
    std::array<float, kSsdCoordinateCount> variance{};
};

struct SsdDecodedBox {
    float x_min = 0.0f;
    float y_min = 0.0f;
    float x_max = 0.0f;
    float y_max = 0.0f;
};

struct SsdProposal {
    uint32_t class_id = 0;
    int32_t score = 0;
    SsdDecodedBox box;
};

struct U8C3SamplePoint {
    uint32_t y_offset = 0;
    uint32_t vu_offset = 0;
};

struct ScaledYvuFrame {
    VIDEO_FRAME_INFO_S frame_info{};
    HI_U64 phy_addr = 0;
    HI_VOID *vir_addr = nullptr;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
};

struct IveRgbFrame {
    IVE_IMAGE_S image{};
    HI_U64 phy_addr = 0;
    HI_VOID *vir_addr = nullptr;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
};

struct MotionImage {
    IVE_IMAGE_S image{};
    HI_U64 phy_addr = 0;
    HI_VOID *vir_addr = nullptr;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
};

struct OcclusionImage {
    IVE_IMAGE_S image{};
    HI_U64 phy_addr = 0;
    HI_VOID *vir_addr = nullptr;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
};

struct MotionBlob {
    IVE_DST_MEM_INFO_S mem{};
    HI_VOID *vir_addr = nullptr;
};

bool AddHiU32(HI_U32 value, HI_U32 *total) {
    if (total == nullptr ||
        static_cast<uint64_t>(*total) + value > kMaxHiU32) {
        return false;
    }
    *total += value;
    return true;
}

bool ToHiU32(uint64_t value, HI_U32 *out) {
    if (out == nullptr || value > kMaxHiU32) {
        return false;
    }
    *out = static_cast<HI_U32>(value);
    return true;
}

bool Align16(uint64_t value, HI_U32 *aligned) {
    if (value > kMaxHiU32 - (kNnieAlign16 - 1)) {
        return false;
    }
    const uint64_t aligned_value =
        ((value + kNnieAlign16 - 1) / kNnieAlign16) * kNnieAlign16;
    return ToHiU32(aligned_value, aligned);
}

uint32_t AlignUpU32(uint32_t value, uint32_t alignment) {
    if (alignment == 0) {
        return value;
    }
    return ((value + alignment - 1U) / alignment) * alignment;
}

HI_U32 BlobUnitSize(SVP_BLOB_TYPE_E type) {
    if (type == SVP_BLOB_TYPE_S32 || type == SVP_BLOB_TYPE_VEC_S32 ||
        type == SVP_BLOB_TYPE_SEQ_S32) {
        return static_cast<HI_U32>(sizeof(HI_U32));
    }
    return static_cast<HI_U32>(sizeof(HI_U8));
}

bool ComputeBlobSize(const SVP_NNIE_NODE_S &node, HI_U32 total_step,
                     SVP_BLOB_S *blob, HI_U32 *blob_size) {
    if (blob == nullptr || blob_size == nullptr) {
        return false;
    }
    const HI_U32 unit_size = BlobUnitSize(node.enType);
    HI_U32 stride = 0;
    if (node.enType == SVP_BLOB_TYPE_SEQ_S32) {
        if (!Align16(static_cast<uint64_t>(node.unShape.u32Dim) * unit_size,
                     &stride)) {
            return false;
        }
        const uint64_t size = static_cast<uint64_t>(total_step) * stride;
        if (!ToHiU32(size, blob_size)) {
            return false;
        }
    } else {
        if (!Align16(
                static_cast<uint64_t>(node.unShape.stWhc.u32Width) *
                    unit_size,
                &stride)) {
            return false;
        }
        const uint64_t size = static_cast<uint64_t>(blob->u32Num) * stride *
                              node.unShape.stWhc.u32Height *
                              node.unShape.stWhc.u32Chn;
        if (!ToHiU32(size, blob_size)) {
            return false;
        }
    }
    blob->u32Stride = stride;
    return true;
}

HI_VOID *VirAddrToPointer(HI_U64 vir_addr) {
    return reinterpret_cast<HI_VOID *>(static_cast<HI_UL>(vir_addr));
}

bool IsSupportedCnnNode(const SVP_NNIE_NODE_S &node) {
    if (node.enType == SVP_BLOB_TYPE_SEQ_S32) {
        return false;
    }
    return node.unShape.stWhc.u32Width != 0 &&
           node.unShape.stWhc.u32Height != 0 &&
           node.unShape.stWhc.u32Chn != 0;
}

bool CheckedFrameRange(uint32_t stride, uint32_t width, uint32_t height,
                       uint32_t available_size) {
    if (stride < width || width == 0 || height == 0) {
        return false;
    }
    const uint64_t end =
        static_cast<uint64_t>(stride) * (height - 1U) + width;
    return end <= available_size;
}

bool IsValidYvu420FrameRange(uint32_t stride_y, uint32_t stride_uv,
                             uint32_t width, uint32_t height,
                             uint32_t available_size) {
    const uint64_t y_size = static_cast<uint64_t>(stride_y) * height;
    if (y_size > available_size || y_size > kMaxHiU32) {
        return false;
    }
    const uint32_t uv_available_size =
        available_size - static_cast<uint32_t>(y_size);
    return CheckedFrameRange(stride_y, width, height,
                             static_cast<uint32_t>(y_size)) &&
           CheckedFrameRange(stride_uv, width, height / 2U,
                             uv_available_size);
}

bool BlobDataSize(const SVP_BLOB_S &blob, HI_U32 *size) {
    if (size == nullptr || blob.enType == SVP_BLOB_TYPE_SEQ_S32) {
        return false;
    }
    return ToHiU32(static_cast<uint64_t>(blob.u32Num) * blob.u32Stride *
                       blob.unShape.stWhc.u32Height *
                       blob.unShape.stWhc.u32Chn,
                   size);
}

bool FlushBlob(const SVP_BLOB_S &blob) {
    HI_U32 size = 0;
    if (!BlobDataSize(blob, &size) || blob.u64PhyAddr == 0 ||
        blob.u64VirAddr == 0) {
        return false;
    }
    return HI_MPI_SYS_MmzFlushCache(blob.u64PhyAddr,
                                    VirAddrToPointer(blob.u64VirAddr),
                                    size) == HI_SUCCESS;
}

uint8_t ClampToByte(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

float ClampFloat(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

int32_t QuantizeConfidence(float value) {
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 1.0f) {
        return kSsdQuantBase;
    }
    return static_cast<int32_t>(value * kSsdQuantBase);
}

float QuantizedConfidence(int32_t value) {
    return static_cast<float>(value) / static_cast<float>(kSsdQuantBase);
}

bool ProposalConfidenceGreater(const SsdProposal &lhs,
                               const SsdProposal &rhs) {
    return lhs.score > rhs.score;
}

bool IsValidSsdBox(const SsdDecodedBox &box) {
    return std::isfinite(box.x_min) && std::isfinite(box.y_min) &&
           std::isfinite(box.x_max) && std::isfinite(box.y_max) &&
           box.x_max > box.x_min && box.y_max > box.y_min;
}

float SsdBoxIou(const SsdDecodedBox &lhs, const SsdDecodedBox &rhs) {
    const float x_min = std::max(lhs.x_min, rhs.x_min);
    const float y_min = std::max(lhs.y_min, rhs.y_min);
    const float x_max = std::min(lhs.x_max, rhs.x_max);
    const float y_max = std::min(lhs.y_max, rhs.y_max);
    const float inter_width = std::max(0.0f, x_max - x_min + 1.0f);
    const float inter_height = std::max(0.0f, y_max - y_min + 1.0f);
    const float inter_area = inter_width * inter_height;
    const float lhs_area =
        std::max(0.0f, lhs.x_max - lhs.x_min + 1.0f) *
        std::max(0.0f, lhs.y_max - lhs.y_min + 1.0f);
    const float rhs_area =
        std::max(0.0f, rhs.x_max - rhs.x_min + 1.0f) *
        std::max(0.0f, rhs.y_max - rhs.y_min + 1.0f);
    const float union_area = lhs_area + rhs_area - inter_area;
    if (union_area <= 0.0f) {
        return 0.0f;
    }
    return inter_area / union_area;
}

bool SoftmaxQuantized(const int32_t *src, int32_t *dst) {
    if (src == nullptr || dst == nullptr) {
        return false;
    }
    int32_t max_value = src[0];
    for (uint32_t i = 1; i < kSsdClassCount; ++i) {
        max_value = std::max(max_value, src[i]);
    }

    std::array<float, kSsdClassCount> exp_values{};
    float sum = 0.0f;
    for (uint32_t i = 0; i < kSsdClassCount; ++i) {
        exp_values[i] =
            std::exp(static_cast<float>(src[i] - max_value) /
                     static_cast<float>(kSsdQuantBase));
        sum += exp_values[i];
    }
    if (sum <= 0.0f || !std::isfinite(sum)) {
        return false;
    }
    for (uint32_t i = 0; i < kSsdClassCount; ++i) {
        dst[i] = static_cast<int32_t>(
            exp_values[i] / sum * static_cast<float>(kSsdQuantBase));
    }
    return true;
}

std::vector<SsdPrior> GenerateSsdPriors() {
    std::vector<SsdPrior> priors;
    priors.reserve(kSsdPriorCount);
    for (uint32_t layer = 0; layer < kSsdLayerCount; ++layer) {
        std::array<float, 6> aspect_ratios{};
        uint32_t aspect_count = 0;
        aspect_ratios[aspect_count++] = 1.0f;
        for (uint32_t i = 0; i < kSsdAspectRatioCount[layer]; ++i) {
            const float ratio = kSsdAspectRatios[layer][i];
            if (ratio <= 0.0f || aspect_count + 2 > aspect_ratios.size()) {
                return std::vector<SsdPrior>();
            }
            aspect_ratios[aspect_count++] = ratio;
            aspect_ratios[aspect_count++] = 1.0f / ratio;
        }

        for (uint32_t y = 0; y < kSsdPriorBoxHeight[layer]; ++y) {
            for (uint32_t x = 0; x < kSsdPriorBoxWidth[layer]; ++x) {
                const float center_x =
                    (static_cast<float>(x) + 0.5f) * kSsdPriorStepWidth[layer];
                const float center_y =
                    (static_cast<float>(y) + 0.5f) * kSsdPriorStepHeight[layer];
                const float min_size = kSsdPriorMinSize[layer];

                SsdPrior min_prior;
                min_prior.x_min =
                    static_cast<float>(static_cast<int32_t>(center_x -
                                                            min_size * 0.5f));
                min_prior.y_min =
                    static_cast<float>(static_cast<int32_t>(center_y -
                                                            min_size * 0.5f));
                min_prior.x_max =
                    static_cast<float>(static_cast<int32_t>(center_x +
                                                            min_size * 0.5f));
                min_prior.y_max =
                    static_cast<float>(static_cast<int32_t>(center_y +
                                                            min_size * 0.5f));
                for (uint32_t i = 0; i < kSsdCoordinateCount; ++i) {
                    min_prior.variance[i] =
                        QuantizedConfidence(kSsdPriorVariance[i]);
                }
                priors.push_back(min_prior);

                const float max_size =
                    std::sqrt(min_size * kSsdPriorMaxSize[layer]);
                SsdPrior max_prior;
                max_prior.x_min =
                    static_cast<float>(static_cast<int32_t>(center_x -
                                                            max_size * 0.5f));
                max_prior.y_min =
                    static_cast<float>(static_cast<int32_t>(center_y -
                                                            max_size * 0.5f));
                max_prior.x_max =
                    static_cast<float>(static_cast<int32_t>(center_x +
                                                            max_size * 0.5f));
                max_prior.y_max =
                    static_cast<float>(static_cast<int32_t>(center_y +
                                                            max_size * 0.5f));
                for (uint32_t i = 0; i < kSsdCoordinateCount; ++i) {
                    max_prior.variance[i] =
                        QuantizedConfidence(kSsdPriorVariance[i]);
                }
                priors.push_back(max_prior);

                for (uint32_t i = 1; i < aspect_count; ++i) {
                    const float ratio_sqrt = std::sqrt(aspect_ratios[i]);
                    const float box_width = min_size * ratio_sqrt;
                    const float box_height = min_size / ratio_sqrt;
                    SsdPrior ratio_prior;
                    ratio_prior.x_min = static_cast<float>(
                        static_cast<int32_t>(center_x - box_width * 0.5f));
                    ratio_prior.y_min = static_cast<float>(
                        static_cast<int32_t>(center_y - box_height * 0.5f));
                    ratio_prior.x_max = static_cast<float>(
                        static_cast<int32_t>(center_x + box_width * 0.5f));
                    ratio_prior.y_max = static_cast<float>(
                        static_cast<int32_t>(center_y + box_height * 0.5f));
                    for (uint32_t j = 0; j < kSsdCoordinateCount; ++j) {
                        ratio_prior.variance[j] =
                            QuantizedConfidence(kSsdPriorVariance[j]);
                    }
                    priors.push_back(ratio_prior);
                }
            }
        }
    }
    if (priors.size() != kSsdPriorCount) {
        return std::vector<SsdPrior>();
    }
    return priors;
}

bool DecodeSsdBoxes(const std::vector<int32_t> &loc_predictions,
                    const std::vector<SsdPrior> &priors,
                    std::vector<SsdDecodedBox> *boxes) {
    if (boxes == nullptr || priors.size() != kSsdPriorCount ||
        loc_predictions.size() != kSsdPriorCount * kSsdCoordinateCount) {
        return false;
    }
    boxes->clear();
    boxes->reserve(kSsdPriorCount);
    for (uint32_t i = 0; i < kSsdPriorCount; ++i) {
        const SsdPrior &prior = priors[i];
        const float prior_width = prior.x_max - prior.x_min;
        const float prior_height = prior.y_max - prior.y_min;
        const float prior_center_x = (prior.x_max + prior.x_min) * 0.5f;
        const float prior_center_y = (prior.y_max + prior.y_min) * 0.5f;
        const uint32_t loc_offset = i * kSsdCoordinateCount;
        const float loc_x = QuantizedConfidence(loc_predictions[loc_offset]);
        const float loc_y = QuantizedConfidence(loc_predictions[loc_offset + 1]);
        const float loc_w = QuantizedConfidence(loc_predictions[loc_offset + 2]);
        const float loc_h = QuantizedConfidence(loc_predictions[loc_offset + 3]);

        const float box_center_x =
            prior.variance[0] * loc_x * prior_width + prior_center_x;
        const float box_center_y =
            prior.variance[1] * loc_y * prior_height + prior_center_y;
        const float box_width =
            std::exp(prior.variance[2] * loc_w) * prior_width;
        const float box_height =
            std::exp(prior.variance[3] * loc_h) * prior_height;

        SsdDecodedBox box;
        box.x_min = static_cast<float>(
            static_cast<int32_t>(box_center_x - box_width * 0.5f));
        box.y_min = static_cast<float>(
            static_cast<int32_t>(box_center_y - box_height * 0.5f));
        box.x_max = static_cast<float>(
            static_cast<int32_t>(box_center_x + box_width * 0.5f));
        box.y_max = static_cast<float>(
            static_cast<int32_t>(box_center_y + box_height * 0.5f));
        boxes->push_back(box);
    }
    return true;
}

void AppendNmsProposals(std::vector<SsdProposal> *proposals,
                        std::vector<uint8_t> *suppressed,
                        std::vector<SsdProposal> *result) {
    if (proposals == nullptr || suppressed == nullptr || result == nullptr ||
        proposals->empty()) {
        return;
    }
    std::sort(proposals->begin(), proposals->end(), ProposalConfidenceGreater);
    if (proposals->size() > kSsdTopK) {
        proposals->resize(kSsdTopK);
    }

    suppressed->assign(proposals->size(), 0);
    for (size_t i = 0; i < proposals->size(); ++i) {
        if ((*suppressed)[i] != 0) {
            continue;
        }
        result->push_back((*proposals)[i]);
        for (size_t j = i + 1; j < proposals->size(); ++j) {
            if ((*suppressed)[j] == 0 &&
                SsdBoxIou((*proposals)[i].box, (*proposals)[j].box) >
                    kSsdNmsThreshold) {
                (*suppressed)[j] = 1;
            }
        }
    }
}
#endif

class HostStubAiEngine final : public AiInferenceEngine {
public:
    const char *Name() const override { return "host_stub"; }
    bool Available() const override { return true; }

    bool Start(const AiModelConfig &config) override {
        started_ = IsValidAiConfig(config);
        if (started_) {
            sequence_ = 0;
        }
        return started_;
    }

    void Stop() override { started_ = false; }

    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config) override {
        AiInferenceResult result;
        result.success = started_ && frame.buffer && frame.size > 0;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        if (!result.success) {
            return result;
        }
        result.sequence = ++sequence_;
        if (config.max_results == 0) {
            return result;
        }
        AiDetection detection = DetectionForTask(config.task);
        if (detection.confidence >= config.confidence_threshold) {
            result.detections.push_back(detection);
        }
        return result;
    }

private:
    AiDetection DetectionForTask(AiTask task) const {
        AiDetection detection;
        switch (task) {
            case AiTask::kFaceDetection:
                detection.label = "face";
                detection.confidence = 0.82f;
                detection.x = 0.42f;
                detection.y = 0.18f;
                detection.width = 0.16f;
                detection.height = 0.22f;
                return detection;
            case AiTask::kMotionClassification:
                detection.label = "motion";
                detection.confidence = 0.79f;
                detection.x = 0.12f;
                detection.y = 0.18f;
                detection.width = 0.46f;
                detection.height = 0.34f;
                return detection;
            case AiTask::kOcclusionDetection:
                detection.label = "occlusion";
                detection.confidence = 0.88f;
                detection.x = 0.0f;
                detection.y = 0.0f;
                detection.width = 1.0f;
                detection.height = 1.0f;
                return detection;
            case AiTask::kObjectDetection:
                detection.label = "person";
                detection.confidence = 0.86f;
                detection.x = 0.18f;
                detection.y = 0.22f;
                detection.width = 0.2f;
                detection.height = 0.46f;
                return detection;
        }
        return detection;
    }

    FrameSequence sequence_ = 0;
    bool started_ = false;
};

class Hi3516Dv300NnieEngine final : public AiInferenceEngine {
public:
    const char *Name() const override { return "hisi3516dv300_nnie"; }
    bool Available() const override { return LIVE_STREAM_HAS_HISI_NNIE != 0; }

    bool Start(const AiModelConfig &config) override {
        if (!IsValidAiConfig(config)) {
            return false;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
        Stop();
        if (config.task == AiTask::kMotionClassification) {
            if (!StartMotionBackend(config)) {
                return false;
            }
            model_path_ = config.model_path;
            started_ = true;
            return true;
        }
        if (config.task == AiTask::kOcclusionDetection) {
            if (!StartOcclusionBackend(config)) {
                return false;
            }
            model_path_ = config.model_path;
            started_ = true;
            return true;
        }
        if (config.task == AiTask::kFaceDetection) {
            Error("ai", "Face detection model is not available");
            return false;
        }
        if (config.model_path.empty()) {
            return false;
        }
        if (!LoadModel(config)) {
            return false;
        }
        model_path_ = config.model_path;
        started_ = true;
        return true;
#else
        (void)config;
        return false;
#endif
    }

    void Stop() override {
#if LIVE_STREAM_HAS_HISI_NNIE
        UnloadModel();
        StopMotionBackend();
        StopOcclusionBackend();
#endif
        model_path_.clear();
        started_ = false;
    }

    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config) override {
        AiInferenceResult result;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        if (!started_ || !frame.buffer || frame.size == 0) {
            return result;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
        if (occlusion_started_) {
            return RunOcclusionDetection(frame, stream_id, config);
        }
        if (motion_started_) {
            return RunMotionDetection(frame, stream_id, config);
        }
        if (!model_loaded_) {
            return result;
        }
        if (!FillInputBlob(frame, config)) {
            return result;
        }
        if (!RunSingleSegForward()) {
            return result;
        }
        result.success = true;
        if (config.task == AiTask::kObjectDetection && ssd_model_ready_) {
            result.detections = DecodeSsdDetections(config);
        }
#else
        (void)config;
#endif
        return result;
    }

private:
#if LIVE_STREAM_HAS_HISI_NNIE
    bool LoadModel(const AiModelConfig &config) {
        const std::string &model_path = config.model_path;
        const std::string model_data = infra::File::ReadAll(model_path);
        if (model_data.empty() ||
            model_data.size() > static_cast<size_t>(0xffffffffU)) {
            Error("ai", "Read NNIE model failed: path=%s",
                  model_path.c_str());
            return false;
        }

        HI_U64 model_phy_addr = 0;
        HI_VOID *model_vir_addr = nullptr;
        const HI_U32 model_size = static_cast<HI_U32>(model_data.size());
        HI_S32 ret = HI_MPI_SYS_MmzAlloc(&model_phy_addr, &model_vir_addr,
                                         "LIVE_AI_NNIE_MODEL", nullptr,
                                         model_size);
        if (ret != HI_SUCCESS || model_phy_addr == 0 ||
            model_vir_addr == nullptr) {
            Error("ai", "Allocate NNIE model MMZ failed: ret=%#x",
                  static_cast<unsigned int>(ret));
            return false;
        }

        std::memcpy(model_vir_addr, model_data.data(), model_data.size());
        model_buf_.u32Size = model_size;
        model_buf_.u64PhyAddr = model_phy_addr;
        model_buf_.u64VirAddr =
            static_cast<HI_U64>(reinterpret_cast<HI_UL>(model_vir_addr));
        std::memset(&model_, 0, sizeof(model_));

        ret = HI_MPI_SVP_NNIE_LoadModel(&model_buf_, &model_);
        if (ret != HI_SUCCESS) {
            Error("ai", "Load NNIE model failed: ret=%#x",
                  static_cast<unsigned int>(ret));
            HI_MPI_SYS_MmzFree(model_buf_.u64PhyAddr, model_vir_addr);
            std::memset(&model_buf_, 0, sizeof(model_buf_));
            std::memset(&model_, 0, sizeof(model_));
            return false;
        }

        model_loaded_ = true;
        if (!PrepareForwardWorkspace()) {
            UnloadModel();
            return false;
        }
        if (!ValidateInputConfig(config)) {
            UnloadModel();
            return false;
        }
        if (!PrepareSsdPostprocess()) {
            UnloadModel();
            return false;
        }
        return true;
    }

    void UnloadModel() {
        FreeForwardWorkspace();
        if (model_loaded_) {
            const HI_S32 ret = HI_MPI_SVP_NNIE_UnloadModel(&model_);
            if (ret != HI_SUCCESS) {
                Error("ai", "Unload NNIE model failed: ret=%#x",
                      static_cast<unsigned int>(ret));
            }
            model_loaded_ = false;
        }

        if (model_buf_.u64PhyAddr != 0 && model_buf_.u64VirAddr != 0) {
            HI_MPI_SYS_MmzFree(
                model_buf_.u64PhyAddr,
                reinterpret_cast<HI_VOID *>(
                    static_cast<HI_UL>(model_buf_.u64VirAddr)));
        }
        std::memset(&model_buf_, 0, sizeof(model_buf_));
        std::memset(&model_, 0, sizeof(model_));
    }

    bool PrepareForwardWorkspace() {
        if (!ValidateLoadedModel() || !FillForwardInfo()) {
            return false;
        }

        HI_U32 total_task_size = 0;
        HI_U32 total_workspace_size = 0;
        HI_S32 ret = HI_MPI_SVP_NNIE_GetTskBufSize(
            kNnieMaxInputNum, 0, &model_, task_buf_sizes_,
            model_.u32NetSegNum);
        if (ret != HI_SUCCESS) {
            Error("ai", "Get NNIE task buffer size failed: ret=%#x",
                  static_cast<unsigned int>(ret));
            return false;
        }
        for (HI_U32 i = 0; i < model_.u32NetSegNum; ++i) {
            if (!AddHiU32(task_buf_sizes_[i], &total_task_size)) {
                return false;
            }
        }
        tmp_buf_size_ = model_.u32TmpBufSize;
        if (!AddHiU32(total_task_size, &total_workspace_size) ||
            !AddHiU32(tmp_buf_size_, &total_workspace_size) ||
            !AddBlobSizes(&total_workspace_size)) {
            return false;
        }

        HI_U64 workspace_phy_addr = 0;
        HI_VOID *workspace_vir_addr = nullptr;
        ret = HI_MPI_SYS_MmzAlloc_Cached(&workspace_phy_addr,
                                         &workspace_vir_addr,
                                         "LIVE_AI_NNIE_TASK", nullptr,
                                         total_workspace_size);
        if (ret != HI_SUCCESS || workspace_phy_addr == 0 ||
            workspace_vir_addr == nullptr) {
            Error("ai", "Allocate NNIE workspace failed: ret=%#x",
                  static_cast<unsigned int>(ret));
            return false;
        }
        std::memset(workspace_vir_addr, 0, total_workspace_size);
        ret = HI_MPI_SYS_MmzFlushCache(workspace_phy_addr, workspace_vir_addr,
                                       total_workspace_size);
        if (ret != HI_SUCCESS) {
            Error("ai", "Flush NNIE workspace failed: ret=%#x",
                  static_cast<unsigned int>(ret));
            HI_MPI_SYS_MmzFree(workspace_phy_addr, workspace_vir_addr);
            return false;
        }

        workspace_buf_.u32Size = total_workspace_size;
        workspace_buf_.u64PhyAddr = workspace_phy_addr;
        workspace_buf_.u64VirAddr =
            static_cast<HI_U64>(reinterpret_cast<HI_UL>(workspace_vir_addr));
        FillWorkspaceAddresses(total_task_size, tmp_buf_size_);
        return true;
    }

    bool ValidateLoadedModel() const {
        if (model_.u32NetSegNum == 0 ||
            model_.u32NetSegNum > SVP_NNIE_MAX_NET_SEG_NUM) {
            Error("ai", "Invalid NNIE segment count: count=%u",
                  static_cast<unsigned int>(model_.u32NetSegNum));
            return false;
        }
        for (HI_U32 i = 0; i < model_.u32NetSegNum; ++i) {
            const SVP_NNIE_SEG_S &seg = model_.astSeg[i];
            if (seg.u16SrcNum == 0 || seg.u16SrcNum > SVP_NNIE_MAX_INPUT_NUM ||
                seg.u16DstNum > SVP_NNIE_MAX_OUTPUT_NUM ||
                seg.enNetType != SVP_NNIE_NET_TYPE_CNN) {
                Error(
                    "ai",
                    "Unsupported NNIE segment: index=%u type=%d src=%u dst=%u",
                    static_cast<unsigned int>(i),
                    static_cast<int>(seg.enNetType),
                    static_cast<unsigned int>(seg.u16SrcNum),
                    static_cast<unsigned int>(seg.u16DstNum));
                return false;
            }
            for (HI_U32 j = 0; j < seg.u16SrcNum; ++j) {
                if (!IsSupportedCnnNode(seg.astSrcNode[j])) {
                    Error("ai",
                          "Unsupported NNIE src node: seg=%u node=%u",
                          static_cast<unsigned int>(i),
                          static_cast<unsigned int>(j));
                    return false;
                }
            }
            for (HI_U32 j = 0; j < seg.u16DstNum; ++j) {
                if (!IsSupportedCnnNode(seg.astDstNode[j])) {
                    Error("ai",
                          "Unsupported NNIE dst node: seg=%u node=%u",
                          static_cast<unsigned int>(i),
                          static_cast<unsigned int>(j));
                    return false;
                }
            }
        }
        return true;
    }

    bool ValidateForwardConfig() const {
        if (model_.u32NetSegNum != 1) {
            Error("ai",
                  "Unsupported NNIE forward segment count: count=%u",
                  static_cast<unsigned int>(model_.u32NetSegNum));
            return false;
        }
        return true;
    }

    bool ValidateInputConfig(const AiModelConfig &config) const {
        const SVP_SRC_BLOB_S &src = seg_data_[0].src[0];
        const bool dims_match =
            src.unShape.stWhc.u32Width == config.input_width &&
            src.unShape.stWhc.u32Height == config.input_height;
        const bool direct_yuv =
            src.enType == SVP_BLOB_TYPE_YVU420SP &&
            src.unShape.stWhc.u32Chn == 3 && dims_match;
        const bool planar_u8 =
            src.enType == SVP_BLOB_TYPE_U8 &&
            src.unShape.stWhc.u32Chn == 3 && dims_match;
        if (!direct_yuv && !planar_u8) {
            Error(
                "ai",
                "Unsupported NNIE input: type=%d chn=%u model=%ux%u "
                "config=%ux%u",
                static_cast<int>(src.enType),
                static_cast<unsigned int>(src.unShape.stWhc.u32Chn),
                static_cast<unsigned int>(src.unShape.stWhc.u32Width),
                static_cast<unsigned int>(src.unShape.stWhc.u32Height),
                static_cast<unsigned int>(config.input_width),
                static_cast<unsigned int>(config.input_height));
            return false;
        }
        return true;
    }

    bool IsSsdModel() const {
        if (model_.u32NetSegNum != 1) {
            return false;
        }
        const SVP_NNIE_SEG_S &seg = model_.astSeg[0];
        const SVP_SRC_BLOB_S &src = seg_data_[0].src[0];
        if (seg.u16SrcNum != 1 || seg.u16DstNum != kSsdReportNodeCount ||
            src.enType != SVP_BLOB_TYPE_U8 ||
            src.unShape.stWhc.u32Chn != 3 ||
            src.unShape.stWhc.u32Width != kSsdInputWidth ||
            src.unShape.stWhc.u32Height != kSsdInputHeight) {
            return false;
        }
        for (uint32_t i = 0; i < kSsdReportNodeCount; ++i) {
            if (seg_data_[0].dst[i].enType != SVP_BLOB_TYPE_S32) {
                return false;
            }
        }
        return true;
    }

    bool FillForwardInfo() {
        std::memset(seg_data_, 0, sizeof(seg_data_));
        std::memset(forward_ctrl_, 0, sizeof(forward_ctrl_));
        for (HI_U32 i = 0; i < model_.u32NetSegNum; ++i) {
            const SVP_NNIE_SEG_S &seg = model_.astSeg[i];
            forward_ctrl_[i].enNnieId = SVP_NNIE_ID_0;
            forward_ctrl_[i].u32SrcNum = seg.u16SrcNum;
            forward_ctrl_[i].u32DstNum = seg.u16DstNum;
            forward_ctrl_[i].u32NetSegId = i;

            for (HI_U32 j = 0; j < seg.u16SrcNum; ++j) {
                seg_data_[i].src[j].enType = seg.astSrcNode[j].enType;
                seg_data_[i].src[j].u32Num = kNnieMaxInputNum;
                seg_data_[i].src[j].unShape.stWhc.u32Chn =
                    seg.astSrcNode[j].unShape.stWhc.u32Chn;
                seg_data_[i].src[j].unShape.stWhc.u32Height =
                    seg.astSrcNode[j].unShape.stWhc.u32Height;
                seg_data_[i].src[j].unShape.stWhc.u32Width =
                    seg.astSrcNode[j].unShape.stWhc.u32Width;
            }
            for (HI_U32 j = 0; j < seg.u16DstNum; ++j) {
                seg_data_[i].dst[j].enType = seg.astDstNode[j].enType;
                seg_data_[i].dst[j].u32Num = kNnieMaxInputNum;
                seg_data_[i].dst[j].unShape.stWhc.u32Chn =
                    seg.astDstNode[j].unShape.stWhc.u32Chn;
                seg_data_[i].dst[j].unShape.stWhc.u32Height =
                    seg.astDstNode[j].unShape.stWhc.u32Height;
                seg_data_[i].dst[j].unShape.stWhc.u32Width =
                    seg.astDstNode[j].unShape.stWhc.u32Width;
            }
        }
        return true;
    }

    bool AddBlobSizes(HI_U32 *total_workspace_size) {
        if (total_workspace_size == nullptr) {
            return false;
        }
        std::memset(blob_sizes_, 0, sizeof(blob_sizes_));
        for (HI_U32 i = 0; i < model_.u32NetSegNum; ++i) {
            const SVP_NNIE_SEG_S &seg = model_.astSeg[i];
            if (i == 0) {
                for (HI_U32 j = 0; j < seg.u16SrcNum; ++j) {
                    if (!ComputeBlobSize(seg.astSrcNode[j], 0,
                                         &seg_data_[i].src[j],
                                         &blob_sizes_[i].src[j]) ||
                        !AddHiU32(blob_sizes_[i].src[j],
                                  total_workspace_size)) {
                        return false;
                    }
                }
            }
            for (HI_U32 j = 0; j < seg.u16DstNum; ++j) {
                if (!ComputeBlobSize(seg.astDstNode[j], 0,
                                     &seg_data_[i].dst[j],
                                     &blob_sizes_[i].dst[j]) ||
                    !AddHiU32(blob_sizes_[i].dst[j],
                              total_workspace_size)) {
                    return false;
                }
            }
        }
        return true;
    }

    void FillWorkspaceAddresses(HI_U32 total_task_size, HI_U32 tmp_buf_size) {
        SVP_MEM_INFO_S task_buf;
        task_buf.u32Size = total_task_size;
        task_buf.u64PhyAddr = workspace_buf_.u64PhyAddr;
        task_buf.u64VirAddr = workspace_buf_.u64VirAddr;

        tmp_buf_.u32Size = tmp_buf_size;
        tmp_buf_.u64PhyAddr = workspace_buf_.u64PhyAddr + total_task_size;
        tmp_buf_.u64VirAddr = workspace_buf_.u64VirAddr + total_task_size;

        HI_U32 task_offset = 0;
        for (HI_U32 i = 0; i < model_.u32NetSegNum; ++i) {
            forward_ctrl_[i].stTmpBuf = tmp_buf_;
            forward_ctrl_[i].stTskBuf.u32Size = task_buf_sizes_[i];
            forward_ctrl_[i].stTskBuf.u64PhyAddr =
                task_buf.u64PhyAddr + task_offset;
            forward_ctrl_[i].stTskBuf.u64VirAddr =
                task_buf.u64VirAddr + task_offset;
            task_offset += task_buf_sizes_[i];
        }

        HI_U64 current_phy_addr =
            workspace_buf_.u64PhyAddr + total_task_size + tmp_buf_size;
        HI_U64 current_vir_addr =
            workspace_buf_.u64VirAddr + total_task_size + tmp_buf_size;
        for (HI_U32 i = 0; i < model_.u32NetSegNum; ++i) {
            const SVP_NNIE_SEG_S &seg = model_.astSeg[i];
            if (i == 0) {
                for (HI_U32 j = 0; j < seg.u16SrcNum; ++j) {
                    seg_data_[i].src[j].u64PhyAddr = current_phy_addr;
                    seg_data_[i].src[j].u64VirAddr = current_vir_addr;
                    current_phy_addr += blob_sizes_[i].src[j];
                    current_vir_addr += blob_sizes_[i].src[j];
                }
            }
            for (HI_U32 j = 0; j < seg.u16DstNum; ++j) {
                seg_data_[i].dst[j].u64PhyAddr = current_phy_addr;
                seg_data_[i].dst[j].u64VirAddr = current_vir_addr;
                current_phy_addr += blob_sizes_[i].dst[j];
                current_vir_addr += blob_sizes_[i].dst[j];
            }
        }
    }

    void FreeForwardWorkspace() {
        if (workspace_buf_.u64PhyAddr != 0 && workspace_buf_.u64VirAddr != 0) {
            HI_MPI_SYS_MmzFree(workspace_buf_.u64PhyAddr,
                               VirAddrToPointer(workspace_buf_.u64VirAddr));
        }
        FreeScaledYvuFrame();
        FreeIveRgbFrame();
        std::memset(&workspace_buf_, 0, sizeof(workspace_buf_));
        std::memset(&tmp_buf_, 0, sizeof(tmp_buf_));
        std::memset(task_buf_sizes_, 0, sizeof(task_buf_sizes_));
        std::memset(blob_sizes_, 0, sizeof(blob_sizes_));
        std::memset(seg_data_, 0, sizeof(seg_data_));
        std::memset(forward_ctrl_, 0, sizeof(forward_ctrl_));
        tmp_buf_size_ = 0;
        ClearU8C3SampleMap();
        ClearSsdPostprocessCache();
    }

    bool StartMotionBackend(const AiModelConfig &config) {
        if (config.input_width == 0 || config.input_height == 0) {
            return false;
        }
        (void)config;
        motion_started_ = false;
        if (HI_IVS_MD_Init() != HI_SUCCESS) {
            Error("ai", "Init IVS motion detection failed");
            return false;
        }
        motion_initialized_ = true;
        motion_started_ = true;
        return true;
    }

    void StopMotionBackend() {
        if (motion_channel_created_) {
            const HI_S32 ret = HI_IVS_MD_DestroyChn(kMotionChannel);
            if (ret != HI_SUCCESS) {
                Error("ai",
                      "Destroy IVS motion channel failed: ret=%#x",
                      static_cast<unsigned int>(ret));
            }
            motion_channel_created_ = false;
        }
        if (motion_initialized_) {
            const HI_S32 ret = HI_IVS_MD_Exit();
            if (ret != HI_SUCCESS) {
                Error("ai", "Exit IVS motion detection failed: ret=%#x",
                      static_cast<unsigned int>(ret));
            }
            motion_initialized_ = false;
        }
        motion_started_ = false;
        FreeMotionWorkspace();
        motion_current_index_ = 0;
        motion_has_reference_ = false;
    }

    void FreeMotionWorkspace() {
        for (MotionImage &image : motion_images_) {
            if (image.phy_addr != 0 && image.vir_addr != nullptr) {
                HI_MPI_SYS_MmzFree(image.phy_addr, image.vir_addr);
            }
            image = MotionImage{};
        }
        if (motion_blob_.mem.u64PhyAddr != 0 && motion_blob_.vir_addr) {
            HI_MPI_SYS_MmzFree(motion_blob_.mem.u64PhyAddr,
                               motion_blob_.vir_addr);
        }
        motion_blob_ = MotionBlob{};
        std::memset(&motion_attr_, 0, sizeof(motion_attr_));
    }

    bool EnsureMotionWorkspace(uint32_t width, uint32_t height) {
        if (motion_images_[0].phy_addr != 0 &&
            motion_images_[0].vir_addr != nullptr &&
            motion_images_[0].width == width &&
            motion_images_[0].height == height &&
            motion_blob_.mem.u64PhyAddr != 0) {
            return true;
        }
        const bool recreate_channel = motion_channel_created_;
        if (recreate_channel) {
            const HI_S32 ret = HI_IVS_MD_DestroyChn(kMotionChannel);
            if (ret != HI_SUCCESS) {
                Error("ai",
                      "Destroy IVS motion channel failed: ret=%#x",
                      static_cast<unsigned int>(ret));
                return false;
            }
            motion_channel_created_ = false;
        }
        FreeMotionWorkspace();

        for (MotionImage &image : motion_images_) {
            if (!AllocMotionImage(width, height, &image)) {
                FreeMotionWorkspace();
                return false;
            }
        }
        if (!AllocMotionBlob()) {
            FreeMotionWorkspace();
            return false;
        }
        InitMotionAttr(width, height);
        if (HI_IVS_MD_CreateChn(kMotionChannel, &motion_attr_) != HI_SUCCESS) {
            Error("ai", "Create IVS motion channel failed");
            FreeMotionWorkspace();
            return false;
        }
        motion_channel_created_ = true;
        motion_has_reference_ = false;
        motion_current_index_ = 0;
        return true;
    }

    bool AllocMotionImage(uint32_t width, uint32_t height,
                          MotionImage *motion_image) {
        if (motion_image == nullptr || width == 0 || height == 0) {
            return false;
        }
        const uint32_t stride = AlignUpU32(width, kIveImageAlign);
        const uint64_t image_size = static_cast<uint64_t>(stride) * height;
        if (stride < width || image_size == 0 || image_size > kMaxHiU32) {
            return false;
        }
        HI_U64 phy_addr = 0;
        HI_VOID *vir_addr = nullptr;
        HI_S32 ret = HI_MPI_SYS_MmzAlloc(&phy_addr, &vir_addr,
                                         "LIVE_AI_MD_IMAGE", nullptr,
                                         static_cast<HI_U32>(image_size));
        if (ret != HI_SUCCESS || phy_addr == 0 || vir_addr == nullptr) {
            return false;
        }
        std::memset(vir_addr, 0, static_cast<size_t>(image_size));
        motion_image->phy_addr = phy_addr;
        motion_image->vir_addr = vir_addr;
        motion_image->size = static_cast<uint32_t>(image_size);
        motion_image->width = width;
        motion_image->height = height;
        motion_image->stride = stride;
        IVE_IMAGE_S &image = motion_image->image;
        std::memset(&image, 0, sizeof(image));
        image.enType = IVE_IMAGE_TYPE_U8C1;
        image.u32Width = width;
        image.u32Height = height;
        image.au32Stride[0] = stride;
        image.au64PhyAddr[0] = phy_addr;
        image.au64VirAddr[0] =
            static_cast<HI_U64>(reinterpret_cast<HI_UL>(vir_addr));
        return true;
    }

    bool AllocMotionBlob() {
        HI_U64 phy_addr = 0;
        HI_VOID *vir_addr = nullptr;
        const HI_U32 blob_size = static_cast<HI_U32>(sizeof(IVE_CCBLOB_S));
        HI_S32 ret = HI_MPI_SYS_MmzAlloc(&phy_addr, &vir_addr,
                                         "LIVE_AI_MD_BLOB", nullptr,
                                         blob_size);
        if (ret != HI_SUCCESS || phy_addr == 0 || vir_addr == nullptr) {
            return false;
        }
        std::memset(vir_addr, 0, blob_size);
        motion_blob_.mem.u64PhyAddr = phy_addr;
        motion_blob_.mem.u64VirAddr =
            static_cast<HI_U64>(reinterpret_cast<HI_UL>(vir_addr));
        motion_blob_.mem.u32Size = blob_size;
        motion_blob_.vir_addr = vir_addr;
        return true;
    }

    void InitMotionAttr(uint32_t width, uint32_t height) {
        std::memset(&motion_attr_, 0, sizeof(motion_attr_));
        motion_attr_.enAlgMode = MD_ALG_MODE_BG;
        motion_attr_.enSadMode = IVE_SAD_MODE_MB_4X4;
        motion_attr_.enSadOutCtrl = IVE_SAD_OUT_CTRL_THRESH;
        motion_attr_.u32Width = width;
        motion_attr_.u32Height = height;
        motion_attr_.u16SadThr = kMotionSadThreshold;
        motion_attr_.stAddCtrl.u0q16X = kMotionBackgroundBlend;
        motion_attr_.stAddCtrl.u0q16Y = kMotionBackgroundBlend;
        motion_attr_.stCclCtrl.enMode = IVE_CCL_MODE_4C;
        const HI_U8 window_size =
            static_cast<HI_U8>(1U << (2U + motion_attr_.enSadMode));
        motion_attr_.stCclCtrl.u16InitAreaThr = window_size * window_size;
        motion_attr_.stCclCtrl.u16Step = window_size;
    }

    bool CopyFrameLumaToMotionImage(const hisisdk::YuvFrame &frame,
                                    MotionImage *motion_image) const {
        if (motion_image == nullptr || motion_image->phy_addr == 0 ||
            motion_image->vir_addr == nullptr ||
            motion_image->width != frame.width ||
            motion_image->height != frame.height ||
            !CanUseMotionFrame(frame)) {
            return false;
        }
        IVE_SRC_DATA_S src{};
        src.u64PhyAddr = frame.mpp_info.phy_addr[0];
        src.u32Width = frame.width;
        src.u32Height = frame.height;
        src.u32Stride = frame.mpp_info.stride[0];
        IVE_DST_DATA_S dst{};
        dst.u64PhyAddr = motion_image->image.au64PhyAddr[0];
        dst.u32Width = motion_image->image.u32Width;
        dst.u32Height = motion_image->image.u32Height;
        dst.u32Stride = motion_image->image.au32Stride[0];
        IVE_DMA_CTRL_S ctrl{};
        ctrl.enMode = IVE_DMA_MODE_DIRECT_COPY;
        IVE_HANDLE handle = 0;
        HI_S32 ret = HI_MPI_IVE_DMA(&handle, &src, &dst, &ctrl, HI_TRUE);
        if (ret != HI_SUCCESS) {
            return false;
        }
        return QueryIveTask(handle);
    }

    bool CanUseMotionFrame(const hisisdk::YuvFrame &frame) const {
        const hisisdk::MppYuvFrameInfo &info = frame.mpp_info;
        return motion_initialized_ && info.valid && frame.width != 0 &&
               frame.height != 0 &&
               info.phy_addr[0] != 0 && info.stride[0] >= frame.width &&
               info.pixel_format ==
                   static_cast<int32_t>(PIXEL_FORMAT_YVU_SEMIPLANAR_420) &&
               info.compress_mode == static_cast<int32_t>(COMPRESS_MODE_NONE);
    }

    AiInferenceResult RunMotionDetection(const hisisdk::YuvFrame &frame,
                                         StreamId stream_id,
                                         const AiModelConfig &config) {
        AiInferenceResult result;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        if (config.max_results == 0 || !CanUseMotionFrame(frame) ||
            !EnsureMotionWorkspace(frame.width, frame.height)) {
            return result;
        }

        MotionImage &current = motion_images_[motion_current_index_];
        if (!CopyFrameLumaToMotionImage(frame, &current)) {
            return result;
        }
        if (!motion_has_reference_) {
            motion_has_reference_ = true;
            motion_current_index_ = 1U - motion_current_index_;
            result.success = true;
            result.sequence = ++motion_sequence_;
            return result;
        }

        MotionImage &reference = motion_images_[1U - motion_current_index_];
        std::memset(motion_blob_.vir_addr, 0, motion_blob_.mem.u32Size);
        HI_S32 ret = HI_IVS_MD_Process(kMotionChannel, &current.image,
                                       &reference.image, nullptr,
                                       &motion_blob_.mem);
        if (ret != HI_SUCCESS) {
            return result;
        }
        result.success = true;
        result.sequence = ++motion_sequence_;
        result.detections = DecodeMotionBlob(config);
        motion_current_index_ = 1U - motion_current_index_;
        return result;
    }

    std::vector<AiDetection> DecodeMotionBlob(const AiModelConfig &config) {
        std::vector<AiDetection> detections;
        if (motion_blob_.vir_addr == nullptr || motion_attr_.u32Width == 0 ||
            motion_attr_.u32Height == 0) {
            return detections;
        }
        const IVE_CCBLOB_S *blob =
            static_cast<const IVE_CCBLOB_S *>(motion_blob_.vir_addr);

        std::vector<const IVE_REGION_S *> regions;
        regions.reserve(std::min<uint32_t>(IVE_MAX_REGION_NUM,
                                           config.max_results));
        HI_U16 area_threshold = 0;
        if (blob->u8RegionNum > config.max_results) {
            area_threshold = blob->u16CurAreaThr;
            while (CountMotionRegions(*blob, area_threshold) >
                   config.max_results) {
                if (area_threshold >
                    std::numeric_limits<HI_U16>::max() -
                        kMotionAreaThresholdStep) {
                    break;
                }
                area_threshold += kMotionAreaThresholdStep;
            }
        }
        for (uint32_t i = 0; i < IVE_MAX_REGION_NUM; ++i) {
            const IVE_REGION_S &region = blob->astRegion[i];
            if (region.u32Area > area_threshold &&
                IsValidMotionRegion(region)) {
                regions.push_back(&region);
            }
        }
        std::sort(regions.begin(), regions.end(),
                  [](const IVE_REGION_S *lhs, const IVE_REGION_S *rhs) {
                      return lhs->u32Area > rhs->u32Area;
                  });
        if (regions.size() > config.max_results) {
            regions.resize(config.max_results);
        }

        detections.reserve(regions.size());
        const float frame_area = static_cast<float>(motion_attr_.u32Width) *
                                 static_cast<float>(motion_attr_.u32Height);
        for (const IVE_REGION_S *region : regions) {
            AiDetection detection;
            detection.label = "motion";
            const float area_ratio =
                static_cast<float>(region->u32Area) / frame_area;
            detection.confidence = ClampFloat(0.5f + area_ratio * 8.0f,
                                              0.0f, 1.0f);
            detection.x = static_cast<float>(region->u16Left) /
                          static_cast<float>(motion_attr_.u32Width);
            detection.y = static_cast<float>(region->u16Top) /
                          static_cast<float>(motion_attr_.u32Height);
            detection.width =
                static_cast<float>(region->u16Right - region->u16Left + 1U) /
                static_cast<float>(motion_attr_.u32Width);
            detection.height =
                static_cast<float>(region->u16Bottom - region->u16Top + 1U) /
                static_cast<float>(motion_attr_.u32Height);
            if (detection.confidence >= config.confidence_threshold) {
                detections.push_back(detection);
            }
        }
        return detections;
    }

    uint32_t CountMotionRegions(const IVE_CCBLOB_S &blob,
                                HI_U16 area_threshold) const {
        uint32_t count = 0;
        for (uint32_t i = 0; i < IVE_MAX_REGION_NUM; ++i) {
            if (blob.astRegion[i].u32Area > area_threshold) {
                ++count;
            }
        }
        return count;
    }

    bool IsValidMotionRegion(const IVE_REGION_S &region) const {
        return region.u32Area != 0 && region.u16Right >= region.u16Left &&
               region.u16Bottom >= region.u16Top &&
               region.u16Right < motion_attr_.u32Width &&
               region.u16Bottom < motion_attr_.u32Height;
    }

    bool StartOcclusionBackend(const AiModelConfig &config) {
        if (config.input_width == 0 || config.input_height == 0) {
            return false;
        }
        (void)config;
        occlusion_started_ = false;
        std::memset(&occlusion_integ_ctrl_, 0,
                    sizeof(occlusion_integ_ctrl_));
        occlusion_integ_ctrl_.enOutCtrl = IVE_INTEG_OUT_CTRL_COMBINE;
        occlusion_started_ = true;
        return true;
    }

    void StopOcclusionBackend() {
        occlusion_started_ = false;
        FreeOcclusionWorkspace();
        std::memset(&occlusion_integ_ctrl_, 0,
                    sizeof(occlusion_integ_ctrl_));
        occlusion_sequence_ = 0;
    }

    void FreeOcclusionWorkspace() {
        FreeOcclusionImage(&occlusion_src_image_);
        FreeOcclusionImage(&occlusion_integ_image_);
    }

    void FreeOcclusionImage(OcclusionImage *occlusion_image) {
        if (occlusion_image == nullptr) {
            return;
        }
        if (occlusion_image->phy_addr != 0 &&
            occlusion_image->vir_addr != nullptr) {
            HI_MPI_SYS_MmzFree(occlusion_image->phy_addr,
                               occlusion_image->vir_addr);
        }
        *occlusion_image = OcclusionImage{};
    }

    bool EnsureOcclusionWorkspace(uint32_t width, uint32_t height) {
        if (occlusion_src_image_.phy_addr != 0 &&
            occlusion_src_image_.vir_addr != nullptr &&
            occlusion_src_image_.width == width &&
            occlusion_src_image_.height == height &&
            occlusion_integ_image_.phy_addr != 0 &&
            occlusion_integ_image_.vir_addr != nullptr &&
            occlusion_integ_image_.width == width &&
            occlusion_integ_image_.height == height) {
            return true;
        }
        if (width < kOcclusionGridWidth || height < kOcclusionGridHeight) {
            return false;
        }
        FreeOcclusionWorkspace();
        if (!AllocOcclusionImage(IVE_IMAGE_TYPE_U8C1,
                                 static_cast<uint32_t>(sizeof(HI_U8)),
                                 "LIVE_AI_OD_SRC", width, height,
                                 &occlusion_src_image_)) {
            FreeOcclusionWorkspace();
            return false;
        }
        if (!AllocOcclusionImage(IVE_IMAGE_TYPE_U64C1,
                                 static_cast<uint32_t>(sizeof(HI_U64)),
                                 "LIVE_AI_OD_INTEG", width, height,
                                 &occlusion_integ_image_)) {
            FreeOcclusionWorkspace();
            return false;
        }
        return true;
    }

    bool AllocOcclusionImage(IVE_IMAGE_TYPE_E image_type,
                             uint32_t element_size,
                             const char *mmz_name,
                             uint32_t width,
                             uint32_t height,
                             OcclusionImage *occlusion_image) {
        if (occlusion_image == nullptr || element_size == 0 || width == 0 ||
            height == 0) {
            return false;
        }
        const uint32_t stride = AlignUpU32(width, kIveImageAlign);
        const uint64_t image_size = static_cast<uint64_t>(stride) * height *
                                    element_size;
        if (stride < width || image_size == 0 || image_size > kMaxHiU32) {
            return false;
        }
        HI_U64 phy_addr = 0;
        HI_VOID *vir_addr = nullptr;
        HI_S32 ret = HI_MPI_SYS_MmzAlloc(
            &phy_addr, &vir_addr, mmz_name, nullptr,
            static_cast<HI_U32>(image_size));
        if (ret != HI_SUCCESS || phy_addr == 0 || vir_addr == nullptr) {
            return false;
        }
        std::memset(vir_addr, 0, static_cast<size_t>(image_size));
        occlusion_image->phy_addr = phy_addr;
        occlusion_image->vir_addr = vir_addr;
        occlusion_image->size = static_cast<uint32_t>(image_size);
        occlusion_image->width = width;
        occlusion_image->height = height;
        occlusion_image->stride = stride;
        IVE_IMAGE_S &image = occlusion_image->image;
        std::memset(&image, 0, sizeof(image));
        image.enType = image_type;
        image.u32Width = width;
        image.u32Height = height;
        image.au32Stride[0] = stride;
        image.au64PhyAddr[0] = phy_addr;
        image.au64VirAddr[0] =
            static_cast<HI_U64>(reinterpret_cast<HI_UL>(vir_addr));
        return true;
    }

    bool CopyFrameLumaToOcclusionImage(
        const hisisdk::YuvFrame &frame,
        OcclusionImage *occlusion_image) const {
        if (occlusion_image == nullptr || occlusion_image->phy_addr == 0 ||
            occlusion_image->vir_addr == nullptr ||
            occlusion_image->width != frame.width ||
            occlusion_image->height != frame.height ||
            !CanUseOcclusionFrame(frame)) {
            return false;
        }
        IVE_SRC_DATA_S src{};
        src.u64PhyAddr = frame.mpp_info.phy_addr[0];
        src.u32Width = frame.width;
        src.u32Height = frame.height;
        src.u32Stride = frame.mpp_info.stride[0];
        IVE_DST_DATA_S dst{};
        dst.u64PhyAddr = occlusion_image->image.au64PhyAddr[0];
        dst.u32Width = occlusion_image->image.u32Width;
        dst.u32Height = occlusion_image->image.u32Height;
        dst.u32Stride = occlusion_image->image.au32Stride[0];
        IVE_DMA_CTRL_S ctrl{};
        ctrl.enMode = IVE_DMA_MODE_DIRECT_COPY;
        IVE_HANDLE handle = 0;
        HI_S32 ret = HI_MPI_IVE_DMA(&handle, &src, &dst, &ctrl, HI_TRUE);
        if (ret != HI_SUCCESS) {
            return false;
        }
        return QueryIveTask(handle);
    }

    bool CanUseOcclusionFrame(const hisisdk::YuvFrame &frame) const {
        const hisisdk::MppYuvFrameInfo &info = frame.mpp_info;
        return occlusion_started_ && info.valid && frame.width != 0 &&
               frame.height != 0 &&
               info.phy_addr[0] != 0 && info.stride[0] >= frame.width &&
               info.pixel_format ==
                   static_cast<int32_t>(PIXEL_FORMAT_YVU_SEMIPLANAR_420) &&
               info.compress_mode == static_cast<int32_t>(COMPRESS_MODE_NONE);
    }

    AiInferenceResult RunOcclusionDetection(
        const hisisdk::YuvFrame &frame,
        StreamId stream_id,
        const AiModelConfig &config) {
        AiInferenceResult result;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        if (config.max_results == 0 || !CanUseOcclusionFrame(frame) ||
            !EnsureOcclusionWorkspace(frame.width, frame.height)) {
            return result;
        }
        if (!CopyFrameLumaToOcclusionImage(frame, &occlusion_src_image_)) {
            return result;
        }

        std::memset(occlusion_integ_image_.vir_addr, 0,
                    occlusion_integ_image_.size);
        IVE_HANDLE handle = 0;
        HI_S32 ret = HI_MPI_IVE_Integ(&handle, &occlusion_src_image_.image,
                                      &occlusion_integ_image_.image,
                                      &occlusion_integ_ctrl_, HI_TRUE);
        if (ret != HI_SUCCESS || !QueryIveTask(handle)) {
            return result;
        }

        result.success = true;
        result.sequence = ++occlusion_sequence_;
        const uint32_t hit_count = CountOcclusionHits();
        if (hit_count > kOcclusionHitThreshold) {
            AiDetection detection;
            detection.label = "occlusion";
            detection.confidence =
                static_cast<float>(hit_count) /
                static_cast<float>(kOcclusionGridCount);
            detection.x = 0.0f;
            detection.y = 0.0f;
            detection.width = 1.0f;
            detection.height = 1.0f;
            if (detection.confidence >= config.confidence_threshold) {
                result.detections.push_back(detection);
            }
        }
        return result;
    }

    uint32_t CountOcclusionHits() const {
        if (occlusion_integ_image_.vir_addr == nullptr ||
            occlusion_integ_image_.width < kOcclusionGridWidth ||
            occlusion_integ_image_.height < kOcclusionGridHeight) {
            return 0;
        }
        const uint32_t block_width =
            occlusion_integ_image_.width / kOcclusionGridWidth;
        const uint32_t block_height =
            occlusion_integ_image_.height / kOcclusionGridHeight;
        if (block_width == 0 || block_height == 0) {
            return 0;
        }

        uint32_t hit_count = 0;
        const HI_U64 *integral = static_cast<const HI_U64 *>(
            occlusion_integ_image_.vir_addr);
        for (uint32_t grid_y = 0; grid_y < kOcclusionGridHeight; ++grid_y) {
            for (uint32_t grid_x = 0; grid_x < kOcclusionGridWidth; ++grid_x) {
                uint32_t mean = 0;
                uint32_t sigma = 0;
                if (ReadOcclusionBlockStats(integral, block_width,
                                            block_height, grid_x, grid_y,
                                            &mean, &sigma) &&
                    IsOcclusionBlock(mean, sigma)) {
                    ++hit_count;
                }
            }
        }
        return hit_count;
    }

    bool ReadOcclusionBlockStats(const HI_U64 *integral,
                                 uint32_t block_width,
                                 uint32_t block_height,
                                 uint32_t grid_x,
                                 uint32_t grid_y,
                                 uint32_t *mean,
                                 uint32_t *sigma) const {
        if (integral == nullptr || mean == nullptr || sigma == nullptr ||
            block_width == 0 || block_height == 0) {
            return false;
        }
        const uint32_t left = grid_x * block_width;
        const uint32_t top = grid_y * block_height;
        const uint32_t right = (grid_x + 1U) * block_width - 1U;
        const uint32_t bottom = (grid_y + 1U) * block_height - 1U;
        const uint32_t stride = occlusion_integ_image_.stride;
        const HI_U64 *top_row =
            top == 0 ? integral : integral + (top - 1U) * stride;
        const HI_U64 *bottom_row = integral + bottom * stride;
        const HI_U64 top_left =
            top == 0 || left == 0 ? 0 : top_row[left - 1U];
        const HI_U64 top_right = top == 0 ? 0 : top_row[right];
        const HI_U64 bottom_left = left == 0 ? 0 : bottom_row[left - 1U];
        const HI_U64 bottom_right = bottom_row[right];
        const int64_t block_sum =
            static_cast<int64_t>(top_left & kOcclusionIntegSumMask) +
            static_cast<int64_t>(bottom_right & kOcclusionIntegSumMask) -
            static_cast<int64_t>(bottom_left & kOcclusionIntegSumMask) -
            static_cast<int64_t>(top_right & kOcclusionIntegSumMask);
        const int64_t block_square =
            static_cast<int64_t>(top_left >> kOcclusionIntegSquareShift) +
            static_cast<int64_t>(bottom_right >>
                                 kOcclusionIntegSquareShift) -
            static_cast<int64_t>(bottom_left >>
                                 kOcclusionIntegSquareShift) -
            static_cast<int64_t>(top_right >>
                                 kOcclusionIntegSquareShift);
        if (block_sum < 0 || block_square < 0) {
            return false;
        }
        const double block_area =
            static_cast<double>(block_width) * static_cast<double>(block_height);
        const double mean_value = static_cast<double>(block_sum) / block_area;
        const double variance =
            static_cast<double>(block_square) / block_area -
            mean_value * mean_value;
        *mean = static_cast<uint32_t>(mean_value);
        *sigma = static_cast<uint32_t>(std::sqrt(std::max(0.0, variance)));
        return true;
    }

    bool IsOcclusionBlock(uint32_t mean, uint32_t sigma) const {
        const int64_t line_mean_delta =
            kOcclusionLineMean1 - kOcclusionLineMean0;
        if (line_mean_delta == 0) {
            return static_cast<int64_t>(mean) <= kOcclusionLineMean0;
        }
        const int64_t lhs =
            (static_cast<int64_t>(sigma) - kOcclusionLineSigma0) *
            line_mean_delta;
        const int64_t rhs =
            (static_cast<int64_t>(mean) - kOcclusionLineMean0) *
            (kOcclusionLineSigma1 - kOcclusionLineSigma0);
        return lhs <= rhs;
    }

    void FreeScaledYvuFrame() {
        if (scaled_yvu_frame_.phy_addr != 0 && scaled_yvu_frame_.vir_addr) {
            HI_MPI_SYS_MmzFree(scaled_yvu_frame_.phy_addr,
                               scaled_yvu_frame_.vir_addr);
        }
        scaled_yvu_frame_ = ScaledYvuFrame{};
    }

    void FreeIveRgbFrame() {
        if (ive_rgb_frame_.phy_addr != 0 && ive_rgb_frame_.vir_addr) {
            HI_MPI_SYS_MmzFree(ive_rgb_frame_.phy_addr,
                               ive_rgb_frame_.vir_addr);
        }
        ive_rgb_frame_ = IveRgbFrame{};
    }

    bool ValidateYuvFrameRange(const hisisdk::YuvFrame &frame,
                               uint32_t *available_size) const {
        if (available_size == nullptr || frame.buffer == nullptr ||
            frame.buffer->data == nullptr ||
            frame.offset > frame.buffer->size ||
            frame.size > frame.buffer->size - frame.offset ||
            frame.width == 0 || frame.height == 0 ||
            (frame.width % 2U) != 0 || (frame.height % 2U) != 0 ||
            frame.stride_y < frame.width || frame.stride_uv < frame.width) {
            return false;
        }

        *available_size =
            std::min(frame.size, frame.buffer->size - frame.offset);
        const uint64_t y_size =
            static_cast<uint64_t>(frame.stride_y) * frame.height;
        const uint64_t uv_size =
            static_cast<uint64_t>(frame.stride_uv) * (frame.height / 2U);
        if (y_size + uv_size > *available_size || y_size > kMaxHiU32) {
            return false;
        }
        return true;
    }

    bool FillInputBlob(const hisisdk::YuvFrame &frame,
                       const AiModelConfig &config) {
        SVP_SRC_BLOB_S &src = seg_data_[0].src[0];
        if (src.enType == SVP_BLOB_TYPE_YVU420SP) {
            return FillYvu420spInputBlob(frame, config);
        }
        if (src.enType == SVP_BLOB_TYPE_U8) {
            return FillU8C3InputBlob(frame);
        }
        return false;
    }

    bool FillYvu420spInputBlob(const hisisdk::YuvFrame &frame,
                               const AiModelConfig &config) {
        SVP_SRC_BLOB_S &src = seg_data_[0].src[0];
        uint32_t available_size = 0;
        if (src.enType != SVP_BLOB_TYPE_YVU420SP ||
            src.unShape.stWhc.u32Width != config.input_width ||
            src.unShape.stWhc.u32Height != config.input_height ||
            frame.width != config.input_width ||
            frame.height != config.input_height ||
            !ValidateYuvFrameRange(frame, &available_size)) {
            return false;
        }

        const uint8_t *frame_data = frame.buffer->data + frame.offset;
        const uint32_t y_size = frame.stride_y * frame.height;
        if (!IsValidYvu420FrameRange(frame.stride_y, frame.stride_uv,
                                     config.input_width, config.input_height,
                                     available_size)) {
            return false;
        }

        uint8_t *dst = static_cast<uint8_t *>(VirAddrToPointer(src.u64VirAddr));
        if (dst == nullptr || src.u32Stride < config.input_width) {
            return false;
        }
        const uint8_t *src_y = frame_data;
        const uint8_t *src_uv = frame_data + y_size;
        const uint32_t total_rows = config.input_height * 3U / 2U;
        for (uint32_t row = 0; row < config.input_height; ++row) {
            std::memcpy(dst + row * src.u32Stride,
                        src_y + row * frame.stride_y,
                        config.input_width);
        }
        for (uint32_t row = 0; row < config.input_height / 2U; ++row) {
            std::memcpy(dst + (config.input_height + row) * src.u32Stride,
                        src_uv + row * frame.stride_uv,
                        config.input_width);
        }

        const HI_U32 flush_size = total_rows * src.u32Stride;
        const HI_S32 ret = HI_MPI_SYS_MmzFlushCache(src.u64PhyAddr, dst,
                                                    flush_size);
        return ret == HI_SUCCESS;
    }

    bool FillU8C3InputBlob(const hisisdk::YuvFrame &frame) {
        SVP_SRC_BLOB_S &dst_blob = seg_data_[0].src[0];
        uint32_t available_size = 0;
        if (dst_blob.enType != SVP_BLOB_TYPE_U8 ||
            dst_blob.unShape.stWhc.u32Chn != 3 ||
            !ValidateYuvFrameRange(frame, &available_size)) {
            return false;
        }
        const uint32_t dst_width = dst_blob.unShape.stWhc.u32Width;
        const uint32_t dst_height = dst_blob.unShape.stWhc.u32Height;
        if (dst_width == 0 || dst_height == 0 ||
            dst_blob.u32Stride < dst_width) {
            return false;
        }
        if (TryFillU8C3InputBlobWithVgs(frame, dst_width, dst_height)) {
            return true;
        }

        const uint8_t *frame_data = frame.buffer->data + frame.offset;
        const uint32_t y_size = frame.stride_y * frame.height;
        if (!IsValidYvu420FrameRange(frame.stride_y, frame.stride_uv,
                                     frame.width, frame.height,
                                     available_size)) {
            return false;
        }
        if (!EnsureU8C3SampleMap(frame, dst_width, dst_height)) {
            return false;
        }

        uint8_t *dst =
            static_cast<uint8_t *>(VirAddrToPointer(dst_blob.u64VirAddr));
        if (dst == nullptr) {
            return false;
        }
        const uint8_t *src_y = frame_data;
        const uint8_t *src_vu = frame_data + y_size;
        const uint32_t channel_size = dst_blob.u32Stride * dst_height;
        uint8_t *dst_b = dst;
        uint8_t *dst_g = dst + channel_size;
        uint8_t *dst_r = dst + channel_size * 2U;

        for (uint32_t y = 0; y < dst_height; ++y) {
            const uint32_t sample_row = y * dst_width;
            const uint32_t dst_offset = y * dst_blob.u32Stride;
            for (uint32_t x = 0; x < dst_width; ++x) {
                const U8C3SamplePoint &sample =
                    u8c3_sample_points_[sample_row + x];
                const uint8_t y_value = src_y[sample.y_offset];
                const uint8_t v_value = src_vu[sample.vu_offset];
                const uint8_t u_value = src_vu[sample.vu_offset + 1U];
                const int y_scaled = kYuvYTable[y_value];
                const uint8_t r =
                    ClampToByte((y_scaled + kYuvVToRTable[v_value] + 128) >>
                                8);
                const uint8_t g =
                    ClampToByte((y_scaled + kYuvUToGTable[u_value] +
                                 kYuvVToGTable[v_value] + 128) >>
                                8);
                const uint8_t b =
                    ClampToByte((y_scaled + kYuvUToBTable[u_value] + 128) >>
                                8);
                dst_b[dst_offset + x] = b;
                dst_g[dst_offset + x] = g;
                dst_r[dst_offset + x] = r;
            }
        }

        const HI_U32 flush_size =
            dst_blob.u32Stride * dst_height * dst_blob.unShape.stWhc.u32Chn;
        return HI_MPI_SYS_MmzFlushCache(dst_blob.u64PhyAddr, dst,
                                        flush_size) == HI_SUCCESS;
    }

    bool TryFillU8C3InputBlobWithVgs(const hisisdk::YuvFrame &frame,
                                     uint32_t dst_width,
                                     uint32_t dst_height) {
        if (!CanUseVgsScale(frame, dst_width, dst_height) ||
            !EnsureScaledYvuFrame(dst_width, dst_height) ||
            !ScaleFrameWithVgs(frame, &scaled_yvu_frame_)) {
            return false;
        }
        const VIDEO_FRAME_S &scaled = scaled_yvu_frame_.frame_info.stVFrame;
        const uint32_t scaled_size = scaled_yvu_frame_.size;
        const uint32_t y_size = scaled.u32Stride[0] * scaled.u32Height;
        if (!IsValidYvu420FrameRange(scaled.u32Stride[0], scaled.u32Stride[1],
                                     scaled.u32Width, scaled.u32Height,
                                     scaled_size)) {
            return false;
        }
        if (TryFillU8C3InputBlobWithIveCsc(frame, scaled_yvu_frame_)) {
            return true;
        }

        SVP_SRC_BLOB_S &dst_blob = seg_data_[0].src[0];
        uint8_t *dst =
            static_cast<uint8_t *>(VirAddrToPointer(dst_blob.u64VirAddr));
        if (dst == nullptr) {
            return false;
        }
        const uint8_t *src_y =
            static_cast<const uint8_t *>(scaled_yvu_frame_.vir_addr);
        const uint8_t *src_vu = src_y + y_size;
        const uint32_t channel_size = dst_blob.u32Stride * dst_height;
        uint8_t *dst_b = dst;
        uint8_t *dst_g = dst + channel_size;
        uint8_t *dst_r = dst + channel_size * 2U;

        for (uint32_t y = 0; y < dst_height; ++y) {
            const uint32_t dst_offset = y * dst_blob.u32Stride;
            const uint32_t y_offset = y * scaled.u32Stride[0];
            const uint32_t vu_offset = (y / 2U) * scaled.u32Stride[1];
            for (uint32_t x = 0; x < dst_width; ++x) {
                const uint32_t chroma_x = (x / 2U) * 2U;
                const uint8_t y_value = src_y[y_offset + x];
                const uint8_t v_value = src_vu[vu_offset + chroma_x];
                const uint8_t u_value = src_vu[vu_offset + chroma_x + 1U];
                const int y_scaled = kYuvYTable[y_value];
                const uint8_t r =
                    ClampToByte((y_scaled + kYuvVToRTable[v_value] + 128) >>
                                8);
                const uint8_t g =
                    ClampToByte((y_scaled + kYuvUToGTable[u_value] +
                                 kYuvVToGTable[v_value] + 128) >>
                                8);
                const uint8_t b =
                    ClampToByte((y_scaled + kYuvUToBTable[u_value] + 128) >>
                                8);
                dst_b[dst_offset + x] = b;
                dst_g[dst_offset + x] = g;
                dst_r[dst_offset + x] = r;
            }
        }

        const HI_U32 flush_size =
            dst_blob.u32Stride * dst_height * dst_blob.unShape.stWhc.u32Chn;
        return HI_MPI_SYS_MmzFlushCache(dst_blob.u64PhyAddr, dst,
                                        flush_size) == HI_SUCCESS;
    }

    bool TryFillU8C3InputBlobWithIveCsc(
        const hisisdk::YuvFrame &frame, const ScaledYvuFrame &scaled_frame) {
        const VIDEO_FRAME_S &scaled = scaled_frame.frame_info.stVFrame;
        if (scaled.u32Width < kIveCscMinWidth ||
            scaled.u32Height < kIveCscMinHeight ||
            !EnsureIveRgbFrame(scaled.u32Width, scaled.u32Height)) {
            return false;
        }
        IVE_IMAGE_S src = MakeIveYvu420spImage(scaled_frame);
        IVE_CSC_CTRL_S ctrl{};
        ctrl.enMode = CscModeForFrame(frame);
        IVE_HANDLE handle = 0;
        HI_S32 ret =
            HI_MPI_IVE_CSC(&handle, &src, &ive_rgb_frame_.image, &ctrl,
                           HI_TRUE);
        if (ret != HI_SUCCESS) {
            return false;
        }
        if (!QueryIveTask(handle)) {
            return false;
        }
        return CopyIveRgbPlanarToBgrBlob(ive_rgb_frame_.image);
    }

    bool EnsureIveRgbFrame(uint32_t width, uint32_t height) {
        if (ive_rgb_frame_.phy_addr != 0 &&
            ive_rgb_frame_.vir_addr != nullptr &&
            ive_rgb_frame_.width == width &&
            ive_rgb_frame_.height == height) {
            return true;
        }
        FreeIveRgbFrame();

        const uint32_t stride = AlignUpU32(width, kIveImageAlign);
        const uint64_t channel_size = static_cast<uint64_t>(stride) * height;
        const uint64_t frame_size = channel_size * 3U;
        if (stride < width || channel_size == 0 || frame_size > kMaxHiU32) {
            return false;
        }

        HI_U64 phy_addr = 0;
        HI_VOID *vir_addr = nullptr;
        HI_S32 ret = HI_MPI_SYS_MmzAlloc(&phy_addr, &vir_addr,
                                         "LIVE_AI_IVE_CSC", nullptr,
                                         static_cast<HI_U32>(frame_size));
        if (ret != HI_SUCCESS || phy_addr == 0 || vir_addr == nullptr) {
            return false;
        }
        std::memset(vir_addr, 0, static_cast<size_t>(frame_size));

        ive_rgb_frame_.phy_addr = phy_addr;
        ive_rgb_frame_.vir_addr = vir_addr;
        ive_rgb_frame_.size = static_cast<uint32_t>(frame_size);
        ive_rgb_frame_.width = width;
        ive_rgb_frame_.height = height;
        ive_rgb_frame_.stride = stride;
        IVE_IMAGE_S &image = ive_rgb_frame_.image;
        std::memset(&image, 0, sizeof(image));
        image.enType = IVE_IMAGE_TYPE_U8C3_PLANAR;
        image.u32Width = width;
        image.u32Height = height;
        for (uint32_t i = 0; i < 3; ++i) {
            image.au32Stride[i] = stride;
            image.au64PhyAddr[i] = phy_addr + channel_size * i;
            image.au64VirAddr[i] =
                static_cast<HI_U64>(reinterpret_cast<HI_UL>(vir_addr)) +
                channel_size * i;
        }
        return true;
    }

    IVE_IMAGE_S MakeIveYvu420spImage(
        const ScaledYvuFrame &scaled_frame) const {
        const VIDEO_FRAME_S &frame = scaled_frame.frame_info.stVFrame;
        IVE_IMAGE_S image{};
        image.enType = IVE_IMAGE_TYPE_YUV420SP;
        image.u32Width = frame.u32Width;
        image.u32Height = frame.u32Height;
        image.au32Stride[0] = frame.u32Stride[0];
        image.au32Stride[1] = frame.u32Stride[1];
        image.au64PhyAddr[0] = frame.u64PhyAddr[0];
        image.au64PhyAddr[1] = frame.u64PhyAddr[1];
        image.au64VirAddr[0] = frame.u64VirAddr[0];
        image.au64VirAddr[1] = frame.u64VirAddr[1];
        return image;
    }

    IVE_CSC_MODE_E CscModeForFrame(const hisisdk::YuvFrame &frame) const {
        if (frame.mpp_info.color_gamut ==
            static_cast<int32_t>(COLOR_GAMUT_BT601)) {
            return IVE_CSC_MODE_PIC_BT601_YUV2RGB;
        }
        return IVE_CSC_MODE_PIC_BT709_YUV2RGB;
    }

    bool QueryIveTask(IVE_HANDLE handle) const {
        HI_BOOL finished = HI_FALSE;
        HI_S32 ret = HI_MPI_IVE_Query(handle, &finished, HI_TRUE);
        return ret == HI_SUCCESS && finished == HI_TRUE;
    }

    bool CopyIveRgbPlanarToBgrBlob(const IVE_IMAGE_S &rgb) {
        SVP_SRC_BLOB_S &dst_blob = seg_data_[0].src[0];
        const uint32_t width = dst_blob.unShape.stWhc.u32Width;
        const uint32_t height = dst_blob.unShape.stWhc.u32Height;
        if (rgb.enType != IVE_IMAGE_TYPE_U8C3_PLANAR ||
            rgb.u32Width != width || rgb.u32Height != height ||
            rgb.au32Stride[0] < width || rgb.au32Stride[1] < width ||
            rgb.au32Stride[2] < width || dst_blob.u32Stride < width ||
            rgb.au64VirAddr[0] == 0 || rgb.au64VirAddr[1] == 0 ||
            rgb.au64VirAddr[2] == 0) {
            return false;
        }

        uint8_t *dst =
            static_cast<uint8_t *>(VirAddrToPointer(dst_blob.u64VirAddr));
        const uint8_t *src_r =
            static_cast<const uint8_t *>(VirAddrToPointer(rgb.au64VirAddr[0]));
        const uint8_t *src_g =
            static_cast<const uint8_t *>(VirAddrToPointer(rgb.au64VirAddr[1]));
        const uint8_t *src_b =
            static_cast<const uint8_t *>(VirAddrToPointer(rgb.au64VirAddr[2]));
        if (dst == nullptr || src_r == nullptr || src_g == nullptr ||
            src_b == nullptr) {
            return false;
        }

        const uint32_t channel_size = dst_blob.u32Stride * height;
        uint8_t *dst_b = dst;
        uint8_t *dst_g = dst + channel_size;
        uint8_t *dst_r = dst + channel_size * 2U;
        for (uint32_t row = 0; row < height; ++row) {
            const uint32_t dst_offset = row * dst_blob.u32Stride;
            std::memcpy(dst_b + dst_offset, src_b + row * rgb.au32Stride[2],
                        width);
            std::memcpy(dst_g + dst_offset, src_g + row * rgb.au32Stride[1],
                        width);
            std::memcpy(dst_r + dst_offset, src_r + row * rgb.au32Stride[0],
                        width);
        }

        const HI_U32 flush_size =
            dst_blob.u32Stride * height * dst_blob.unShape.stWhc.u32Chn;
        return HI_MPI_SYS_MmzFlushCache(dst_blob.u64PhyAddr, dst,
                                        flush_size) == HI_SUCCESS;
    }

    bool CanUseVgsScale(const hisisdk::YuvFrame &frame, uint32_t dst_width,
                        uint32_t dst_height) const {
        const hisisdk::MppYuvFrameInfo &info = frame.mpp_info;
        return info.valid &&
               info.pixel_format ==
                   static_cast<int32_t>(PIXEL_FORMAT_YVU_SEMIPLANAR_420) &&
               info.compress_mode ==
                   static_cast<int32_t>(COMPRESS_MODE_NONE) &&
               info.phy_addr[0] != 0 && info.width == frame.width &&
               info.height == frame.height && dst_width % 2U == 0 &&
               dst_height % 2U == 0;
    }

    bool EnsureScaledYvuFrame(uint32_t width, uint32_t height) {
        if (scaled_yvu_frame_.phy_addr != 0 &&
            scaled_yvu_frame_.vir_addr != nullptr &&
            scaled_yvu_frame_.width == width &&
            scaled_yvu_frame_.height == height) {
            return true;
        }
        FreeScaledYvuFrame();

        const uint32_t aligned_height = AlignUpU32(height, 2U);
        const uint32_t stride = AlignUpU32(width, kVgsFrameAlign);
        const uint64_t y_size = static_cast<uint64_t>(stride) * aligned_height;
        const uint64_t frame_size = y_size * 3U / 2U;
        if (stride < width || y_size == 0 || frame_size > kMaxHiU32) {
            return false;
        }

        HI_U64 phy_addr = 0;
        HI_VOID *vir_addr = nullptr;
        HI_S32 ret = HI_MPI_SYS_MmzAlloc(&phy_addr, &vir_addr,
                                         "LIVE_AI_VGS_SCALE", nullptr,
                                         static_cast<HI_U32>(frame_size));
        if (ret != HI_SUCCESS || phy_addr == 0 || vir_addr == nullptr) {
            return false;
        }
        std::memset(vir_addr, 0, static_cast<size_t>(frame_size));

        scaled_yvu_frame_.phy_addr = phy_addr;
        scaled_yvu_frame_.vir_addr = vir_addr;
        scaled_yvu_frame_.size = static_cast<uint32_t>(frame_size);
        scaled_yvu_frame_.width = width;
        scaled_yvu_frame_.height = height;
        scaled_yvu_frame_.stride = stride;
        VIDEO_FRAME_INFO_S &frame_info = scaled_yvu_frame_.frame_info;
        std::memset(&frame_info, 0, sizeof(frame_info));
        frame_info.enModId = HI_ID_VGS;
        frame_info.u32PoolId = VB_INVALID_POOLID;
        VIDEO_FRAME_S &frame = frame_info.stVFrame;
        frame.u32Width = width;
        frame.u32Height = height;
        frame.enField = VIDEO_FIELD_FRAME;
        frame.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        frame.enVideoFormat = VIDEO_FORMAT_LINEAR;
        frame.enCompressMode = COMPRESS_MODE_NONE;
        frame.enDynamicRange = DYNAMIC_RANGE_SDR8;
        frame.enColorGamut = COLOR_GAMUT_BT709;
        frame.u32Stride[0] = stride;
        frame.u32Stride[1] = stride;
        frame.u32Stride[2] = stride;
        frame.u64PhyAddr[0] = phy_addr;
        frame.u64PhyAddr[1] = phy_addr + y_size;
        frame.u64PhyAddr[2] = frame.u64PhyAddr[1];
        frame.u64VirAddr[0] =
            static_cast<HI_U64>(reinterpret_cast<HI_UL>(vir_addr));
        frame.u64VirAddr[1] = frame.u64VirAddr[0] + y_size;
        frame.u64VirAddr[2] = frame.u64VirAddr[1];
        return true;
    }

    VIDEO_FRAME_INFO_S MakeInputVideoFrameInfo(
        const hisisdk::YuvFrame &frame) const {
        const hisisdk::MppYuvFrameInfo &info = frame.mpp_info;
        VIDEO_FRAME_INFO_S frame_info{};
        frame_info.u32PoolId = info.pool_id;
        frame_info.enModId = static_cast<MOD_ID_E>(info.module_id);
        VIDEO_FRAME_S &video_frame = frame_info.stVFrame;
        video_frame.u32Width = info.width;
        video_frame.u32Height = info.height;
        video_frame.enField = static_cast<VIDEO_FIELD_E>(info.field);
        video_frame.enPixelFormat =
            static_cast<PIXEL_FORMAT_E>(info.pixel_format);
        video_frame.enVideoFormat =
            static_cast<VIDEO_FORMAT_E>(info.video_format);
        video_frame.enCompressMode =
            static_cast<COMPRESS_MODE_E>(info.compress_mode);
        video_frame.enDynamicRange =
            static_cast<DYNAMIC_RANGE_E>(info.dynamic_range);
        video_frame.enColorGamut = static_cast<COLOR_GAMUT_E>(info.color_gamut);
        for (uint32_t i = 0; i < 3; ++i) {
            video_frame.u32Stride[i] = info.stride[i];
            video_frame.u32HeaderStride[i] = info.header_stride[i];
            video_frame.u32ExtStride[i] = info.ext_stride[i];
            video_frame.u64PhyAddr[i] = info.phy_addr[i];
            video_frame.u64VirAddr[i] = info.vir_addr[i];
            video_frame.u64HeaderPhyAddr[i] = info.header_phy_addr[i];
            video_frame.u64HeaderVirAddr[i] = info.header_vir_addr[i];
            video_frame.u64ExtPhyAddr[i] = info.ext_phy_addr[i];
            video_frame.u64ExtVirAddr[i] = info.ext_vir_addr[i];
        }
        video_frame.s16OffsetTop = info.offset_top;
        video_frame.s16OffsetBottom = info.offset_bottom;
        video_frame.s16OffsetLeft = info.offset_left;
        video_frame.s16OffsetRight = info.offset_right;
        video_frame.u32MaxLuminance = info.max_luminance;
        video_frame.u32MinLuminance = info.min_luminance;
        video_frame.u32TimeRef = info.time_ref;
        video_frame.u64PTS = static_cast<HI_U64>(frame.pts_us);
        video_frame.u32FrameFlag = info.frame_flag;
        if (video_frame.u32Stride[1] == 0) {
            video_frame.u32Stride[1] = video_frame.u32Stride[0];
        }
        if (video_frame.u64PhyAddr[1] == 0 &&
            video_frame.u64PhyAddr[0] != 0 &&
            video_frame.u32Stride[0] != 0) {
            video_frame.u64PhyAddr[1] =
                video_frame.u64PhyAddr[0] +
                static_cast<HI_U64>(video_frame.u32Stride[0]) *
                    video_frame.u32Height;
        }
        if (video_frame.u64VirAddr[1] == 0 &&
            video_frame.u64VirAddr[0] != 0 &&
            video_frame.u32Stride[0] != 0) {
            video_frame.u64VirAddr[1] =
                video_frame.u64VirAddr[0] +
                static_cast<HI_U64>(video_frame.u32Stride[0]) *
                    video_frame.u32Height;
        }
        return frame_info;
    }

    bool ScaleFrameWithVgs(const hisisdk::YuvFrame &frame,
                           ScaledYvuFrame *scaled_frame) {
        if (scaled_frame == nullptr || scaled_frame->phy_addr == 0 ||
            scaled_frame->vir_addr == nullptr) {
            return false;
        }
        VGS_HANDLE handle = -1;
        HI_S32 ret = HI_MPI_VGS_BeginJob(&handle);
        if (ret != HI_SUCCESS) {
            return false;
        }

        VGS_TASK_ATTR_S task{};
        task.stImgIn = MakeInputVideoFrameInfo(frame);
        task.stImgOut = scaled_frame->frame_info;
        ret = HI_MPI_VGS_AddScaleTask(handle, &task, VGS_SCLCOEF_NORMAL);
        if (ret != HI_SUCCESS) {
            HI_MPI_VGS_CancelJob(handle);
            return false;
        }
        ret = HI_MPI_VGS_EndJob(handle);
        if (ret != HI_SUCCESS) {
            HI_MPI_VGS_CancelJob(handle);
            return false;
        }
        return true;
    }

    bool EnsureU8C3SampleMap(const hisisdk::YuvFrame &frame,
                             uint32_t dst_width, uint32_t dst_height) {
        const uint64_t sample_count =
            static_cast<uint64_t>(dst_width) * dst_height;
        if (sample_count == 0 ||
            sample_count > static_cast<uint64_t>(0xffffffffU)) {
            return false;
        }
        if (sample_frame_width_ == frame.width &&
            sample_frame_height_ == frame.height &&
            sample_stride_y_ == frame.stride_y &&
            sample_stride_uv_ == frame.stride_uv &&
            sample_dst_width_ == dst_width &&
            sample_dst_height_ == dst_height &&
            u8c3_sample_points_.size() == static_cast<size_t>(sample_count)) {
            return true;
        }

        std::vector<U8C3SamplePoint> sample_points;
        sample_points.resize(static_cast<size_t>(sample_count));
        for (uint32_t y = 0; y < dst_height; ++y) {
            const uint32_t src_y_row =
                static_cast<uint64_t>(y) * frame.height / dst_height;
            const uint32_t src_uv_row = src_y_row / 2U;
            for (uint32_t x = 0; x < dst_width; ++x) {
                const uint32_t src_x =
                    static_cast<uint64_t>(x) * frame.width / dst_width;
                const uint32_t chroma_x = (src_x / 2U) * 2U;
                U8C3SamplePoint sample;
                sample.y_offset = src_y_row * frame.stride_y + src_x;
                sample.vu_offset = src_uv_row * frame.stride_uv + chroma_x;
                sample_points[static_cast<size_t>(y) * dst_width + x] =
                    sample;
            }
        }

        u8c3_sample_points_.swap(sample_points);
        sample_frame_width_ = frame.width;
        sample_frame_height_ = frame.height;
        sample_stride_y_ = frame.stride_y;
        sample_stride_uv_ = frame.stride_uv;
        sample_dst_width_ = dst_width;
        sample_dst_height_ = dst_height;
        return true;
    }

    void ClearU8C3SampleMap() {
        u8c3_sample_points_.clear();
        sample_frame_width_ = 0;
        sample_frame_height_ = 0;
        sample_stride_y_ = 0;
        sample_stride_uv_ = 0;
        sample_dst_width_ = 0;
        sample_dst_height_ = 0;
    }

    bool RunSingleSegForward() {
        if (!ValidateForwardConfig()) {
            return false;
        }
        SVP_NNIE_FORWARD_CTRL_S &ctrl = forward_ctrl_[0];
        if (HI_MPI_SYS_MmzFlushCache(ctrl.stTskBuf.u64PhyAddr,
                                     VirAddrToPointer(ctrl.stTskBuf.u64VirAddr),
                                     ctrl.stTskBuf.u32Size) != HI_SUCCESS) {
            return false;
        }
        for (HI_U32 i = 0; i < ctrl.u32DstNum; ++i) {
            if (!FlushBlob(seg_data_[0].dst[i])) {
                return false;
            }
        }

        SVP_NNIE_HANDLE handle = 0;
        HI_S32 ret = HI_MPI_SVP_NNIE_Forward(&handle, seg_data_[0].src,
                                             &model_, seg_data_[0].dst,
                                             &ctrl, HI_TRUE);
        if (ret != HI_SUCCESS) {
            return false;
        }

        HI_BOOL finished = HI_FALSE;
        ret = HI_MPI_SVP_NNIE_Query(ctrl.enNnieId, handle, &finished, HI_TRUE);
        if (ret != HI_SUCCESS || finished != HI_TRUE) {
            return false;
        }
        for (HI_U32 i = 0; i < ctrl.u32DstNum; ++i) {
            if (!FlushBlob(seg_data_[0].dst[i])) {
                return false;
            }
        }
        return true;
    }

    bool AppendS32BlobValues(const SVP_DST_BLOB_S &blob,
                             std::vector<int32_t> *values) const {
        if (values == nullptr || blob.enType != SVP_BLOB_TYPE_S32 ||
            blob.u64VirAddr == 0 ||
            blob.u32Stride < blob.unShape.stWhc.u32Width * sizeof(int32_t)) {
            return false;
        }
        const int32_t *data =
            static_cast<const int32_t *>(VirAddrToPointer(blob.u64VirAddr));
        if (data == nullptr) {
            return false;
        }
        const uint32_t stride_words = blob.u32Stride / sizeof(int32_t);
        for (uint32_t n = 0; n < blob.u32Num; ++n) {
            const uint32_t batch_offset =
                n * blob.unShape.stWhc.u32Chn *
                blob.unShape.stWhc.u32Height * stride_words;
            for (uint32_t chn = 0; chn < blob.unShape.stWhc.u32Chn; ++chn) {
                for (uint32_t row = 0; row < blob.unShape.stWhc.u32Height;
                     ++row) {
                    const uint32_t row_offset =
                        batch_offset +
                        (chn * blob.unShape.stWhc.u32Height + row) *
                            stride_words;
                    for (uint32_t col = 0; col < blob.unShape.stWhc.u32Width;
                         ++col) {
                        values->push_back(data[row_offset + col]);
                    }
                }
            }
        }
        return true;
    }

    bool CollectSsdOutputs() {
        ssd_loc_predictions_.clear();
        ssd_conf_scores_.clear();

        for (uint32_t layer = 0; layer < kSsdLayerCount; ++layer) {
            const size_t loc_offset = ssd_loc_predictions_.size();
            if (!AppendS32BlobValues(seg_data_[0].dst[layer * 2U],
                                     &ssd_loc_predictions_) ||
                ssd_loc_predictions_.size() - loc_offset !=
                    kSsdDetectInputChannel[layer]) {
                return false;
            }

            ssd_conf_raw_.clear();
            if (!AppendS32BlobValues(seg_data_[0].dst[layer * 2U + 1U],
                                     &ssd_conf_raw_) ||
                ssd_conf_raw_.size() != kSsdSoftmaxInputChannel[layer] ||
                ssd_conf_raw_.size() % kSsdClassCount != 0) {
                return false;
            }
            for (size_t offset = 0; offset < ssd_conf_raw_.size();
                 offset += kSsdClassCount) {
                const size_t score_offset = ssd_conf_scores_.size();
                ssd_conf_scores_.resize(score_offset + kSsdClassCount);
                if (!SoftmaxQuantized(&ssd_conf_raw_[offset],
                                      &ssd_conf_scores_[score_offset])) {
                    return false;
                }
            }
        }

        return ssd_loc_predictions_.size() ==
                   kSsdPriorCount * kSsdCoordinateCount &&
               ssd_conf_scores_.size() == kSsdPriorCount * kSsdClassCount;
    }

    bool PrepareSsdPostprocess() {
        ssd_model_ready_ = IsSsdModel();
        if (!ssd_model_ready_) {
            ClearSsdPostprocessCache();
            return true;
        }
        ssd_priors_ = GenerateSsdPriors();
        if (ssd_priors_.size() != kSsdPriorCount) {
            Error("ai", "Prepare SSD priors failed");
            ClearSsdPostprocessCache();
            return false;
        }
        ssd_loc_predictions_.reserve(kSsdPriorCount * kSsdCoordinateCount);
        ssd_conf_raw_.reserve(kSsdSoftmaxInputChannel[0]);
        ssd_conf_scores_.reserve(kSsdPriorCount * kSsdClassCount);
        ssd_boxes_.reserve(kSsdPriorCount);
        ssd_class_proposals_.reserve(kSsdPriorCount);
        ssd_proposals_after_nms_.reserve((kSsdClassCount - 1U) * kSsdTopK);
        ssd_nms_suppressed_.reserve(kSsdTopK);
        return true;
    }

    void ClearSsdPostprocessCache() {
        ssd_model_ready_ = false;
        ssd_priors_.clear();
        ssd_loc_predictions_.clear();
        ssd_conf_raw_.clear();
        ssd_conf_scores_.clear();
        ssd_boxes_.clear();
        ssd_class_proposals_.clear();
        ssd_proposals_after_nms_.clear();
        ssd_nms_suppressed_.clear();
    }

    std::vector<AiDetection> DecodeSsdDetections(const AiModelConfig &config) {
        if (!CollectSsdOutputs()) {
            return std::vector<AiDetection>();
        }
        if (!DecodeSsdBoxes(ssd_loc_predictions_, ssd_priors_, &ssd_boxes_)) {
            return std::vector<AiDetection>();
        }

        const int32_t score_threshold =
            QuantizeConfidence(config.confidence_threshold);
        ssd_proposals_after_nms_.clear();
        for (uint32_t class_id = 1; class_id < kSsdClassCount; ++class_id) {
            ssd_class_proposals_.clear();
            for (uint32_t i = 0; i < kSsdPriorCount; ++i) {
                const int32_t score =
                    ssd_conf_scores_[i * kSsdClassCount + class_id];
                if (score < score_threshold || !IsValidSsdBox(ssd_boxes_[i])) {
                    continue;
                }
                SsdProposal proposal;
                proposal.class_id = class_id;
                proposal.score = score;
                proposal.box = ssd_boxes_[i];
                ssd_class_proposals_.push_back(proposal);
            }
            AppendNmsProposals(&ssd_class_proposals_, &ssd_nms_suppressed_,
                               &ssd_proposals_after_nms_);
        }

        std::sort(ssd_proposals_after_nms_.begin(),
                  ssd_proposals_after_nms_.end(),
                  ProposalConfidenceGreater);
        if (ssd_proposals_after_nms_.size() > kSsdKeepTopK) {
            ssd_proposals_after_nms_.resize(kSsdKeepTopK);
        }
        if (ssd_proposals_after_nms_.size() > config.max_results) {
            ssd_proposals_after_nms_.resize(config.max_results);
        }

        std::vector<AiDetection> detections;
        detections.reserve(ssd_proposals_after_nms_.size());
        const float model_width = static_cast<float>(kSsdInputWidth);
        const float model_height = static_cast<float>(kSsdInputHeight);
        for (const SsdProposal &proposal : ssd_proposals_after_nms_) {
            const float x_min =
                ClampFloat(proposal.box.x_min, 0.0f, model_width);
            const float y_min =
                ClampFloat(proposal.box.y_min, 0.0f, model_height);
            const float x_max =
                ClampFloat(proposal.box.x_max, 0.0f, model_width);
            const float y_max =
                ClampFloat(proposal.box.y_max, 0.0f, model_height);
            if (x_max <= x_min || y_max <= y_min) {
                continue;
            }
            AiDetection detection;
            detection.label = kSsdVocLabels[proposal.class_id];
            detection.confidence = QuantizedConfidence(proposal.score);
            detection.x = x_min / model_width;
            detection.y = y_min / model_height;
            detection.width = (x_max - x_min) / model_width;
            detection.height = (y_max - y_min) / model_height;
            detections.push_back(detection);
        }
        return detections;
    }

    SVP_SRC_MEM_INFO_S model_buf_{};
    SVP_NNIE_MODEL_S model_{};
    SVP_MEM_INFO_S workspace_buf_{};
    SVP_MEM_INFO_S tmp_buf_{};
    HI_U32 task_buf_sizes_[SVP_NNIE_MAX_NET_SEG_NUM]{};
    HI_U32 tmp_buf_size_ = 0;
    NnieBlobSize blob_sizes_[SVP_NNIE_MAX_NET_SEG_NUM]{};
    NnieSegData seg_data_[SVP_NNIE_MAX_NET_SEG_NUM]{};
    SVP_NNIE_FORWARD_CTRL_S forward_ctrl_[SVP_NNIE_MAX_NET_SEG_NUM]{};
    ScaledYvuFrame scaled_yvu_frame_;
    IveRgbFrame ive_rgb_frame_;
    std::vector<U8C3SamplePoint> u8c3_sample_points_;
    uint32_t sample_frame_width_ = 0;
    uint32_t sample_frame_height_ = 0;
    uint32_t sample_stride_y_ = 0;
    uint32_t sample_stride_uv_ = 0;
    uint32_t sample_dst_width_ = 0;
    uint32_t sample_dst_height_ = 0;
    std::vector<SsdPrior> ssd_priors_;
    std::vector<int32_t> ssd_loc_predictions_;
    std::vector<int32_t> ssd_conf_raw_;
    std::vector<int32_t> ssd_conf_scores_;
    std::vector<SsdDecodedBox> ssd_boxes_;
    std::vector<SsdProposal> ssd_class_proposals_;
    std::vector<SsdProposal> ssd_proposals_after_nms_;
    std::vector<uint8_t> ssd_nms_suppressed_;
    std::array<MotionImage, kMotionImageCount> motion_images_;
    MotionBlob motion_blob_;
    MD_ATTR_S motion_attr_{};
    FrameSequence motion_sequence_ = 0;
    uint32_t motion_current_index_ = 0;
    OcclusionImage occlusion_src_image_;
    OcclusionImage occlusion_integ_image_;
    IVE_INTEG_CTRL_S occlusion_integ_ctrl_{};
    FrameSequence occlusion_sequence_ = 0;
    bool model_loaded_ = false;
    bool ssd_model_ready_ = false;
    bool motion_initialized_ = false;
    bool motion_channel_created_ = false;
    bool motion_started_ = false;
    bool motion_has_reference_ = false;
    bool occlusion_started_ = false;
#endif
    std::string model_path_;
    bool started_ = false;
};

}  // namespace

std::shared_ptr<AiInferenceEngine> CreateAiEngine(AiBackend backend) {
    if (backend == AiBackend::kHostStub) {
        return std::shared_ptr<AiInferenceEngine>(new HostStubAiEngine());
    }
    return std::shared_ptr<AiInferenceEngine>(new Hi3516Dv300NnieEngine());
}

}  // namespace ai_internal
}  // namespace live_stream
