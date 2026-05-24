#include "ai_service.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "config_service.h"
#include "hisisdk/hisi_sdk.h"
#include "infra/executor.h"
#include "infra/fs.h"
#include "infra/log.h"
#include "infra/time.h"
#include "json_utils.h"
#include "media_service.h"
#include "snapshot_service.h"

#if defined(LIVE_STREAM_ENABLE_HISI_MPP) && \
    defined(LIVE_STREAM_ENABLE_HISI_NNIE) && \
    __has_include("mpi_nnie.h") && __has_include("mpi_sys.h")
#define LIVE_STREAM_HAS_HISI_NNIE 1
extern "C" {
#include "mpi_nnie.h"
#include "mpi_sys.h"
}
#else
#define LIVE_STREAM_HAS_HISI_NNIE 0
#endif

namespace live_stream {
namespace {

constexpr uint32_t kDefaultExecutorQueueCapacity = 8;
constexpr int64_t kMinAlertIntervalMs = 1000;

#if LIVE_STREAM_HAS_HISI_NNIE
constexpr HI_U32 kNnieMaxInputNum = 1;
constexpr HI_U32 kNnieAlign16 = 16;
constexpr uint64_t kMaxHiU32 = std::numeric_limits<HI_U32>::max();

struct NnieSegData {
    SVP_SRC_BLOB_S src[SVP_NNIE_MAX_INPUT_NUM];
    SVP_DST_BLOB_S dst[SVP_NNIE_MAX_OUTPUT_NUM];
};

struct NnieBlobSize {
    HI_U32 src[SVP_NNIE_MAX_INPUT_NUM];
    HI_U32 dst[SVP_NNIE_MAX_OUTPUT_NUM];
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
#endif

bool IsFiniteConfidence(float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

const char *ToString(AiBackend backend) {
    switch (backend) {
        case AiBackend::kHi3516Dv300Nnie:
            return "hisi3516dv300_nnie";
        case AiBackend::kHostStub:
            return "host_stub";
    }
    return "hisi3516dv300_nnie";
}

bool ParseBackend(const std::string &value, AiBackend *backend) {
    if (backend == nullptr) {
        return false;
    }
    if (value == "hisi3516dv300_nnie" || value == "nnie") {
        *backend = AiBackend::kHi3516Dv300Nnie;
        return true;
    }
    if (value == "host_stub") {
        *backend = AiBackend::kHostStub;
        return true;
    }
    return false;
}

bool ParseTask(const std::string &value, AiTask *task) {
    if (task == nullptr) {
        return false;
    }
    if (value == "object_detection") {
        *task = AiTask::kObjectDetection;
        return true;
    }
    if (value == "face_detection") {
        *task = AiTask::kFaceDetection;
        return true;
    }
    if (value == "motion_classification") {
        *task = AiTask::kMotionClassification;
        return true;
    }
    return false;
}

bool ParseStream(const std::string &value, StreamId *stream_id) {
    if (stream_id == nullptr) {
        return false;
    }
    if (value == "main") {
        *stream_id = StreamId::kMain;
        return true;
    }
    if (value == "sub") {
        *stream_id = StreamId::kSub;
        return true;
    }
    return false;
}

bool IsValidConfig(const AiModelConfig &config) {
    if (config.input_width == 0 || config.input_height == 0 ||
        config.inference_interval_ms == 0 || config.max_results == 0 ||
        !IsFiniteConfidence(config.confidence_threshold)) {
        return false;
    }
    if (!config.enabled) {
        return true;
    }
    if (config.backend == AiBackend::kHi3516Dv300Nnie &&
        config.model_path.empty()) {
        return false;
    }
    return config.stream_id == StreamId::kMain ||
           config.stream_id == StreamId::kSub;
}

bool ParseAiConfig(const ConfigJson &value, const AiModelConfig &fallback,
                   AiModelConfig *parsed) {
    if (parsed == nullptr || !value.is_object()) {
        return false;
    }
    AiModelConfig config = fallback;
    std::string backend;
    std::string task;
    std::string stream;
    if (!json_utils::ReadField(value, "enabled", &config.enabled) ||
        !json_utils::ReadField(value, "backend", &backend) ||
        !ParseBackend(backend, &config.backend) ||
        !json_utils::ReadField(value, "task", &task) ||
        !ParseTask(task, &config.task) ||
        !json_utils::ReadField(value, "stream", &stream) ||
        !ParseStream(stream, &config.stream_id) ||
        !json_utils::ReadField(value, "model_path", &config.model_path) ||
        !json_utils::ReadField(value, "input_width", &config.input_width, 1,
                          0xffffffffU) ||
        !json_utils::ReadField(value, "input_height", &config.input_height, 1,
                          0xffffffffU) ||
        !json_utils::ReadField(value, "inference_interval_ms",
                          &config.inference_interval_ms, 1, 0xffffffffU) ||
        !json_utils::ReadField(value, "max_results", &config.max_results, 1,
                          0xffffffffU) ||
        !json_utils::ReadField(value, "confidence_threshold",
                          &config.confidence_threshold, 0.0f, 1.0f)) {
        return false;
    }
    if (!IsValidConfig(config)) {
        return false;
    }
    *parsed = config;
    return true;
}

class AiInferenceEngine {
public:
    virtual ~AiInferenceEngine() = default;

    virtual const char *Name() const = 0;
    virtual bool Available() const = 0;
    virtual bool Start(const AiModelConfig &config) = 0;
    virtual void Stop() = 0;
    virtual AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                                  StreamId stream_id,
                                  const AiModelConfig &config) = 0;
};

class HostStubAiEngine final : public AiInferenceEngine {
public:
    const char *Name() const override { return "host_stub"; }
    bool Available() const override { return true; }

    bool Start(const AiModelConfig &config) override {
        started_ = IsValidConfig(config);
        return started_;
    }

    void Stop() override { started_ = false; }

    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config) override {
        (void)config;
        AiInferenceResult result;
        result.success = started_ && frame.buffer && frame.size > 0;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        return result;
    }

private:
    bool started_ = false;
};

class Hi3516Dv300NnieEngine final : public AiInferenceEngine {
public:
    const char *Name() const override { return "hisi3516dv300_nnie"; }
    bool Available() const override { return LIVE_STREAM_HAS_HISI_NNIE != 0; }

    bool Start(const AiModelConfig &config) override {
        if (!IsValidConfig(config) || config.model_path.empty()) {
            return false;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
        Stop();
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
#endif
        model_path_.clear();
        started_ = false;
    }

    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config) override {
        (void)config;
        AiInferenceResult result;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        if (!started_ || !frame.buffer || frame.size == 0) {
            return result;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
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
        // Output post-processing is task/model specific, so detections stay empty
        // until SSD/YOLO/classifier decoding is implemented.
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
            INFRA_LOG_ERROR("ai", "Read NNIE model failed: path=%s",
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
            INFRA_LOG_ERROR("ai", "Allocate NNIE model MMZ failed: ret=%#x",
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
            INFRA_LOG_ERROR("ai", "Load NNIE model failed: ret=%#x",
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
        return true;
    }

    void UnloadModel() {
        FreeForwardWorkspace();
        if (model_loaded_) {
            const HI_S32 ret = HI_MPI_SVP_NNIE_UnloadModel(&model_);
            if (ret != HI_SUCCESS) {
                INFRA_LOG_ERROR("ai", "Unload NNIE model failed: ret=%#x",
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
            INFRA_LOG_ERROR("ai", "Get NNIE task buffer size failed: ret=%#x",
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
            INFRA_LOG_ERROR("ai", "Allocate NNIE workspace failed: ret=%#x",
                            static_cast<unsigned int>(ret));
            return false;
        }
        std::memset(workspace_vir_addr, 0, total_workspace_size);
        ret = HI_MPI_SYS_MmzFlushCache(workspace_phy_addr, workspace_vir_addr,
                                       total_workspace_size);
        if (ret != HI_SUCCESS) {
            INFRA_LOG_ERROR("ai", "Flush NNIE workspace failed: ret=%#x",
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
            INFRA_LOG_ERROR("ai", "Invalid NNIE segment count: count=%u",
                            static_cast<unsigned int>(model_.u32NetSegNum));
            return false;
        }
        for (HI_U32 i = 0; i < model_.u32NetSegNum; ++i) {
            const SVP_NNIE_SEG_S &seg = model_.astSeg[i];
            if (seg.u16SrcNum == 0 || seg.u16SrcNum > SVP_NNIE_MAX_INPUT_NUM ||
                seg.u16DstNum > SVP_NNIE_MAX_OUTPUT_NUM ||
                seg.enNetType != SVP_NNIE_NET_TYPE_CNN) {
                INFRA_LOG_ERROR(
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
                    INFRA_LOG_ERROR("ai",
                                    "Unsupported NNIE src node: seg=%u node=%u",
                                    static_cast<unsigned int>(i),
                                    static_cast<unsigned int>(j));
                    return false;
                }
            }
            for (HI_U32 j = 0; j < seg.u16DstNum; ++j) {
                if (!IsSupportedCnnNode(seg.astDstNode[j])) {
                    INFRA_LOG_ERROR("ai",
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
            INFRA_LOG_ERROR("ai",
                            "Unsupported NNIE forward segment count: count=%u",
                            static_cast<unsigned int>(model_.u32NetSegNum));
            return false;
        }
        return true;
    }

    bool ValidateInputConfig(const AiModelConfig &config) const {
        const SVP_SRC_BLOB_S &src = seg_data_[0].src[0];
        if (src.enType != SVP_BLOB_TYPE_YVU420SP ||
            src.unShape.stWhc.u32Chn != 3 ||
            src.unShape.stWhc.u32Width != config.input_width ||
            src.unShape.stWhc.u32Height != config.input_height) {
            INFRA_LOG_ERROR(
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
        std::memset(&workspace_buf_, 0, sizeof(workspace_buf_));
        std::memset(&tmp_buf_, 0, sizeof(tmp_buf_));
        std::memset(task_buf_sizes_, 0, sizeof(task_buf_sizes_));
        std::memset(blob_sizes_, 0, sizeof(blob_sizes_));
        std::memset(seg_data_, 0, sizeof(seg_data_));
        std::memset(forward_ctrl_, 0, sizeof(forward_ctrl_));
        tmp_buf_size_ = 0;
    }

    bool FillInputBlob(const hisisdk::YuvFrame &frame,
                       const AiModelConfig &config) {
        SVP_SRC_BLOB_S &src = seg_data_[0].src[0];
        if (src.enType != SVP_BLOB_TYPE_YVU420SP ||
            frame.buffer == nullptr || frame.buffer->data == nullptr ||
            frame.offset > frame.buffer->size ||
            frame.size > frame.buffer->size - frame.offset ||
            frame.width != config.input_width ||
            frame.height != config.input_height ||
            frame.stride_y < config.input_width ||
            frame.stride_uv < config.input_width) {
            return false;
        }

        const uint32_t available_size =
            std::min(frame.size, frame.buffer->size - frame.offset);
        const uint8_t *frame_data = frame.buffer->data + frame.offset;
        const uint32_t y_size = frame.stride_y * frame.height;
        if (y_size > available_size) {
            return false;
        }
        const uint32_t uv_available_size = available_size - y_size;
        if (!CheckedFrameRange(frame.stride_y, config.input_width,
                               config.input_height, y_size) ||
            !CheckedFrameRange(frame.stride_uv, config.input_width,
                               config.input_height / 2, uv_available_size)) {
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
        return ret == HI_SUCCESS && finished == HI_TRUE;
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
    bool model_loaded_ = false;
#endif
    std::string model_path_;
    bool started_ = false;
};

std::unique_ptr<AiInferenceEngine> CreateEngine(AiBackend backend) {
    if (backend == AiBackend::kHostStub) {
        return std::unique_ptr<AiInferenceEngine>(new HostStubAiEngine());
    }
    return std::unique_ptr<AiInferenceEngine>(new Hi3516Dv300NnieEngine());
}

MppChannel VpssChannelForStream(const MediaChannels &channels,
                                StreamId stream_id) {
    return stream_id == StreamId::kSub ? channels.sub_vpss : channels.vpss;
}

hisisdk::Size YuvSizeForStream(const MediaChannels &channels,
                               StreamId stream_id) {
    const VideoSize size = stream_id == StreamId::kSub ? channels.sub_size
                                                       : channels.main_size;
    return hisisdk::Size{size.width, size.height};
}

bool HasAlertDetections(const AiInferenceResult &result) {
    return result.success && !result.detections.empty();
}

float MaxConfidence(const std::vector<AiDetection> &detections) {
    float max_confidence = 0.0f;
    for (const AiDetection &detection : detections) {
        if (detection.confidence > max_confidence) {
            max_confidence = detection.confidence;
        }
    }
    return max_confidence;
}

std::string AlertImagePath(const std::string &dir, const std::string &id) {
    return infra::Path::Join(dir, id + ".jpg");
}

bool LooksLikeJpeg(const SnapshotFrame &frame) {
    const uint8_t *data = frame.PayloadData();
    return data != nullptr && frame.size >= 2 && data[0] == 0xff &&
           data[1] == 0xd8;
}

}  // namespace

struct AiService::Impl final {
    explicit Impl(const AiServiceOptions &service_options)
        : options(service_options), config(service_options.default_config) {
        if (options.max_alert_records == 0) {
            options.max_alert_records = 100;
        }
    }

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!IsValidConfig(config)) {
            return false;
        }
        if (options.config_service != nullptr && !config_attached) {
            ConfigAttachment attachment;
            attachment.validate = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex);
                AiModelConfig parsed;
                return ParseAiConfig(value, config, &parsed)
                           ? ConfigResult::Success()
                           : ConfigResult::Failure("", "invalid ai config");
            };
            attachment.apply = [this](const ConfigJson &value) {
                std::lock_guard<std::mutex> guard(mutex);
                AiModelConfig parsed;
                ParseAiConfig(value, config, &parsed);
                config = parsed;
                return ConfigResult::Success();
            };
            if (!options.config_service->AttachConfig("ai", attachment)) {
                return false;
            }
            config_attached = true;
        }
        if (options.config_service != nullptr) {
            ConfigJson ai_config = options.config_service->GetValue("ai");
            if (ai_config.is_object()) {
                AiModelConfig parsed;
                if (!ParseAiConfig(ai_config, config, &parsed)) {
                    return false;
                }
                config = parsed;
            }
        }
        return true;
    }

    bool Start() {
        if (!Prepare()) {
            return false;
        }

        AiModelConfig start_config;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (started) {
                return true;
            }
            start_config = config;
            stats.enabled = start_config.enabled;
            if (!start_config.enabled) {
                started = true;
                return true;
            }
            engine = CreateEngine(start_config.backend);
            if (!engine || !engine->Available() || !engine->Start(start_config)) {
                INFRA_LOG_ERROR("ai", "Start AI backend failed: backend=%s model=%s",
                                ToString(start_config.backend),
                                start_config.model_path.c_str());
                engine.reset();
                return false;
            }
            stats.backend_available = engine->Available();
            executor.reset(new infra::Executor());
            infra::ExecutorOptions executor_options;
            executor_options.worker_count = 1;
            executor_options.queue_capacity = kDefaultExecutorQueueCapacity;
            if (!executor || !executor->Start(executor_options)) {
                engine->Stop();
                engine.reset();
                executor.reset();
                return false;
            }
            started = true;
        }

        if (options.media_service == nullptr || options.sdk == nullptr ||
            !options.media_service->IsStarted()) {
            Stop();
            return false;
        }
        if (!executor->Post([this]() { CaptureLoop(); })) {
            Stop();
            return false;
        }
        INFRA_LOG_INFO("ai", "AI service started: backend=%s stream=%d",
                       ToString(start_config.backend),
                       static_cast<int>(start_config.stream_id));
        return true;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started && !executor && !engine) {
                return;
            }
            started = false;
        }
        if (executor) {
            executor->Stop(infra::StopMode::kDiscard);
        }
        std::lock_guard<std::mutex> lock(mutex);
        executor.reset();
        if (engine) {
            engine->Stop();
            engine.reset();
        }
        stats.backend_available = false;
    }

    void CaptureLoop() {
        while (true) {
            AiModelConfig run_config;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!started || !config.enabled) {
                    return;
                }
                run_config = config;
            }
            hisisdk::YuvFrame frame = options.sdk->CaptureYuvFrame(
                VpssChannelForStream(options.media_channels,
                                     run_config.stream_id),
                YuvSizeForStream(options.media_channels,
                                 run_config.stream_id),
                run_config.inference_interval_ms);
            if (!frame.buffer || frame.size == 0) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++stats.skipped_frames;
                    ++stats.inference_failed_count;
                }
                infra::Time::SleepMillis(run_config.inference_interval_ms);
                continue;
            }
            RunInference(frame, run_config);
            infra::Time::SleepMillis(run_config.inference_interval_ms);
        }
    }

    void RunInference(const hisisdk::YuvFrame &frame,
                      const AiModelConfig &run_config) {
        AiInferenceResult result;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started || !engine) {
                ++stats.inference_failed_count;
                return;
            }
            ++stats.received_frames;
            result = engine->Run(frame, run_config.stream_id, run_config);
            if (result.success) {
                ++stats.inference_count;
            } else {
                ++stats.inference_failed_count;
            }
            last_result = result;
            stats.active_results =
                static_cast<uint32_t>(last_result.detections.size());
        }
        MaybeSaveAlert(result, run_config);
    }

    void MaybeSaveAlert(const AiInferenceResult &result,
                        const AiModelConfig &run_config) {
        if (!HasAlertDetections(result) ||
            options.snapshot_service == nullptr) {
            return;
        }
        const int64_t now_ms = infra::Time::SystemTimeMillis();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!started || now_ms - last_alert_ms < kMinAlertIntervalMs) {
                return;
            }
            last_alert_ms = now_ms;
        }

        CaptureRequest request;
        request.stream_id = run_config.stream_id;
        request.include_thumbnail = false;
        SnapshotFrame frame = options.snapshot_service->Capture(request);
        if (!LooksLikeJpeg(frame)) {
            std::lock_guard<std::mutex> lock(mutex);
            ++stats.dropped_tasks;
            return;
        }

        if (!infra::Path::MakeDirs(options.alert_image_dir)) {
            std::lock_guard<std::mutex> lock(mutex);
            ++stats.dropped_tasks;
            return;
        }

        const std::string id =
            std::to_string(now_ms) + "-" + std::to_string(next_alert_id++);
        const uint8_t *data = frame.PayloadData();
        std::string image;
        image.assign(reinterpret_cast<const char *>(data), frame.size);
        if (!infra::File::WriteAll(AlertImagePath(options.alert_image_dir, id),
                                   image)) {
            std::lock_guard<std::mutex> lock(mutex);
            ++stats.dropped_tasks;
            return;
        }

        AiAlertRecord alert;
        alert.id = id;
        alert.timestamp_ms = now_ms;
        alert.stream_id = result.stream_id;
        alert.task = run_config.task;
        alert.detection_count =
            static_cast<uint32_t>(result.detections.size());
        alert.max_confidence = MaxConfidence(result.detections);
        alert.detections = result.detections;
        AddAlert(alert);
    }

    void AddAlert(const AiAlertRecord &alert) {
        std::string expired_image_path;
        {
            std::lock_guard<std::mutex> lock(mutex);
            alerts.push_back(alert);
            while (alerts.size() > options.max_alert_records) {
                expired_image_path =
                    AlertImagePath(options.alert_image_dir, alerts.front().id);
                alerts.erase(alerts.begin());
            }
        }
        if (!expired_image_path.empty()) {
            static_cast<void>(infra::File::Remove(expired_image_path));
        }
    }

    AiServiceOptions options;
    AiModelConfig config;
    std::unique_ptr<AiInferenceEngine> engine;
    std::unique_ptr<infra::Executor> executor;
    AiInferenceResult last_result;
    AiServiceStats stats;
    std::vector<AiAlertRecord> alerts;
    uint64_t next_alert_id = 1;
    int64_t last_alert_ms = 0;
    bool config_attached = false;
    bool started = false;
    mutable std::mutex mutex;
};

AiService::AiService() : AiService(AiServiceOptions{}) {}

AiService::AiService(const AiServiceOptions &options)
    : impl_(new Impl(options)) {}

AiService::~AiService() {
    if (impl_) {
        impl_->Stop();
    }
}

bool AiService::Start() { return impl_ != nullptr && impl_->Start(); }

void AiService::Stop() {
    if (impl_) {
        impl_->Stop();
    }
}

AiModelConfig AiService::GetConfig() const {
    if (!impl_) {
        return AiModelConfig{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->config;
}

AiServiceStats AiService::GetStats() const {
    if (!impl_) {
        return AiServiceStats{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    AiServiceStats stats = impl_->stats;
    stats.enabled = impl_->config.enabled;
    stats.backend_available = impl_->engine && impl_->engine->Available();
    return stats;
}

AiInferenceResult AiService::GetLastResult() const {
    if (!impl_) {
        return AiInferenceResult{};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->last_result;
}

std::vector<AiAlertRecord> AiService::ListAlerts() const {
    if (!impl_) {
        return std::vector<AiAlertRecord>();
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<AiAlertRecord> alerts = impl_->alerts;
    std::reverse(alerts.begin(), alerts.end());
    return alerts;
}

std::string AiService::ReadAlertImage(const std::string &id) const {
    if (!impl_ || id.empty()) {
        return std::string();
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto iter = std::find_if(
            impl_->alerts.begin(), impl_->alerts.end(),
            [&id](const AiAlertRecord &alert) { return alert.id == id; });
        if (iter == impl_->alerts.end()) {
            return std::string();
        }
    }
    return infra::File::ReadAll(AlertImagePath(impl_->options.alert_image_dir,
                                               id));
}

const char *AiService::StaticName() { return "ai_service"; }

}  // namespace live_stream
