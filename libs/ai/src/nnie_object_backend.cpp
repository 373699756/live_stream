#include "nnie_object_backend.h"

#include "nnie_input_writer.h"
#include "nnie_model_session.h"

#include "ai_config.h"
#include "hisi_ai_platform.h"
#include "infra/fs.h"
#include "infra/log.h"
#include "ssd_postprocess.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {
namespace ai_internal {
namespace {

#if LIVE_STREAM_HAS_HISI_NNIE
constexpr HI_U32 kNnieMaxInputNum = 1;
constexpr HI_U32 kNnieAlign16 = 16;







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



}  // namespace

struct NnieObjectBackend::Impl {
    bool Start(const AiModelConfig &config) {
        if (config.model_path.empty()) {
            return false;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
        Stop();
        if (!LoadModel(config)) {
            return false;
        }
        started_ = true;
        return true;
#else
        (void)config;
        return false;
#endif
    }

    void Stop() {
#if LIVE_STREAM_HAS_HISI_NNIE
        UnloadModel();
#endif
        started_ = false;
    }

    bool Started() const { return started_; }

    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config) {
        AiInferenceResult result;
        result.stream_id = stream_id;
        result.pts_us = frame.pts_us;
        if (!started_ || !frame.Valid()) {
            return result;
        }
#if LIVE_STREAM_HAS_HISI_NNIE
        if (!model_session_.Loaded()) {
            return result;
        }
        if (!input_writer_.Write(frame, config, &seg_data_[0].src[0])) {
            return result;
        }
        if (!RunSingleSegForward()) {
            return result;
        }
        result.success = true;
        if ((config.task == AiTask::kObjectDetection ||
             config.task == AiTask::kPerimeterDetection) &&
            ssd_model_ready_) {
            result.detections = DecodeSsdDetections(config);
        }
#else
        (void)config;
#endif
        return result;
    }

#if LIVE_STREAM_HAS_HISI_NNIE
    bool LoadModel(const AiModelConfig &config) {
#if LIVE_STREAM_HAS_HISI_NNIE
        if (!model_session_.Load(config)) {
            return false;
        }
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
        if (!ValidateTaskModel(config)) {
            UnloadModel();
            return false;
        }
        return true;
#else
        (void)config;
        return false;
#endif
    }

    void UnloadModel() {
#if LIVE_STREAM_HAS_HISI_NNIE
        FreeForwardWorkspace();
        model_session_.Unload();
#endif
    }

    bool PrepareForwardWorkspace() {
        Info("ai", "NNIE workspace prepare begin: segs=%u",
             static_cast<unsigned int>(model_session_.Model().u32NetSegNum));
        if (!model_session_.Loaded() || !FillForwardInfo()) {
            return false;
        }

        HI_U32 total_task_size = 0;
        HI_U32 total_workspace_size = 0;
        HI_S32 ret = HI_MPI_SVP_NNIE_GetTskBufSize(
            kNnieMaxInputNum, 0, model_session_.MutableModel(), task_buf_sizes_,
            model_session_.Model().u32NetSegNum);
        if (ret != HI_SUCCESS) {
            Error("ai", "Get NNIE task buffer size failed: ret=%#x",
                  static_cast<unsigned int>(ret));
            return false;
        }
        for (HI_U32 i = 0; i < model_session_.Model().u32NetSegNum; ++i) {
            if (!AddHiU32(task_buf_sizes_[i], &total_task_size)) {
                return false;
            }
        }
        tmp_buf_size_ = model_session_.Model().u32TmpBufSize;
        Info("ai", "NNIE task buffers ready: total_task=%u tmp=%u",
             static_cast<unsigned int>(total_task_size),
             static_cast<unsigned int>(tmp_buf_size_));
        if (!AddHiU32(total_task_size, &total_workspace_size) ||
            !AddHiU32(tmp_buf_size_, &total_workspace_size) ||
            !AddBlobSizes(&total_workspace_size)) {
            return false;
        }

        HI_U64 workspace_phy_addr = 0;
        HI_VOID *workspace_vir_addr = nullptr;
        Info("ai", "NNIE workspace MMZ alloc begin: size=%u",
             static_cast<unsigned int>(total_workspace_size));
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
        Info("ai", "NNIE workspace MMZ alloc done: phy=0x%llx vir=%p",
             static_cast<unsigned long long>(workspace_phy_addr),
             workspace_vir_addr);
        Info("ai", "NNIE workspace memset begin: size=%u",
             static_cast<unsigned int>(total_workspace_size));
        std::memset(workspace_vir_addr, 0, total_workspace_size);
        Info("ai", "NNIE workspace memset done: size=%u",
             static_cast<unsigned int>(total_workspace_size));
        Info("ai", "NNIE workspace flush begin: size=%u",
             static_cast<unsigned int>(total_workspace_size));
        ret = HI_MPI_SYS_MmzFlushCache(workspace_phy_addr, workspace_vir_addr,
                                       total_workspace_size);
        if (ret != HI_SUCCESS) {
            Error("ai", "Flush NNIE workspace failed: ret=%#x",
                  static_cast<unsigned int>(ret));
            HI_MPI_SYS_MmzFree(workspace_phy_addr, workspace_vir_addr);
            return false;
        }
        Info("ai", "NNIE workspace flush done: size=%u",
             static_cast<unsigned int>(total_workspace_size));

        workspace_buf_.u32Size = total_workspace_size;
        workspace_buf_.u64PhyAddr = workspace_phy_addr;
        workspace_buf_.u64VirAddr =
            static_cast<HI_U64>(reinterpret_cast<HI_UL>(workspace_vir_addr));
        FillWorkspaceAddresses(total_task_size, tmp_buf_size_);
        return true;
    }


    bool ValidateForwardConfig() const {
        if (model_session_.Model().u32NetSegNum != 1) {
            Error("ai",
                  "Unsupported NNIE forward segment size: size=%u",
                  static_cast<unsigned int>(model_session_.Model().u32NetSegNum));
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

    bool ValidateTaskModel(const AiModelConfig &config) const {
        if (config.task == AiTask::kObjectDetection ||
            config.task == AiTask::kPerimeterDetection) {
            if (ssd_model_ready_) {
                return true;
            }
            Error("ai", "Unsupported object detection NNIE model outputs");
            return false;
        }
        return true;
    }

    bool IsSsdModel() const {
        if (model_session_.Model().u32NetSegNum != 1) {
            return false;
        }
        const SVP_NNIE_SEG_S &seg = model_session_.Model().astSeg[0];
        const SVP_SRC_BLOB_S &src = seg_data_[0].src[0];
        if (seg.u16SrcNum != 1 || seg.u16DstNum != kSsdReportNodes ||
            src.enType != SVP_BLOB_TYPE_U8 ||
            src.unShape.stWhc.u32Chn != 3 ||
            src.unShape.stWhc.u32Width != kSsdInputWidth ||
            src.unShape.stWhc.u32Height != kSsdInputHeight) {
            return false;
        }
        for (uint32_t i = 0; i < kSsdReportNodes; ++i) {
            if (seg_data_[0].dst[i].enType != SVP_BLOB_TYPE_S32) {
                return false;
            }
        }
        return true;
    }

    bool FillForwardInfo() {
        std::memset(seg_data_, 0, sizeof(seg_data_));
        std::memset(forward_ctrl_, 0, sizeof(forward_ctrl_));
        for (HI_U32 i = 0; i < model_session_.Model().u32NetSegNum; ++i) {
            const SVP_NNIE_SEG_S &seg = model_session_.Model().astSeg[i];
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
        for (HI_U32 i = 0; i < model_session_.Model().u32NetSegNum; ++i) {
            const SVP_NNIE_SEG_S &seg = model_session_.Model().astSeg[i];
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
        for (HI_U32 i = 0; i < model_session_.Model().u32NetSegNum; ++i) {
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
        for (HI_U32 i = 0; i < model_session_.Model().u32NetSegNum; ++i) {
            const SVP_NNIE_SEG_S &seg = model_session_.Model().astSeg[i];
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
        input_writer_.Release();
        std::memset(&workspace_buf_, 0, sizeof(workspace_buf_));
        std::memset(&tmp_buf_, 0, sizeof(tmp_buf_));
        std::memset(task_buf_sizes_, 0, sizeof(task_buf_sizes_));
        std::memset(blob_sizes_, 0, sizeof(blob_sizes_));
        std::memset(seg_data_, 0, sizeof(seg_data_));
        std::memset(forward_ctrl_, 0, sizeof(forward_ctrl_));
        tmp_buf_size_ = 0;
        ClearSsdPostprocessCache();
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
                                             model_session_.MutableModel(), seg_data_[0].dst,
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

    bool CopyS32BlobValues(const SVP_DST_BLOB_S &blob,
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
        const uint64_t total_values =
            static_cast<uint64_t>(blob.u32Num) *
            blob.unShape.stWhc.u32Chn *
            blob.unShape.stWhc.u32Height *
            blob.unShape.stWhc.u32Width;
        if (total_values > static_cast<uint64_t>(kMaxHiU32)) {
            return false;
        }
        values->clear();
        values->resize(static_cast<std::size_t>(total_values));
        int32_t *output = values->data();
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
                    const std::size_t row_bytes =
                        static_cast<std::size_t>(blob.unShape.stWhc.u32Width) *
                        sizeof(int32_t);
                    std::memcpy(output, data + row_offset, row_bytes);
                    output += blob.unShape.stWhc.u32Width;
                }
            }
        }
        return true;
    }

    bool CollectSsdOutputs() {
        ssd_postprocess_.BeginFrame();

        for (uint32_t layer = 0; layer < kSsdLayers; ++layer) {
            if (!CopyS32BlobValues(seg_data_[0].dst[layer * 2U],
                                   &ssd_layer_values_) ||
                !ssd_postprocess_.AppendLocationLayer(layer,
                                                      ssd_layer_values_)) {
                return false;
            }

            if (!CopyS32BlobValues(seg_data_[0].dst[layer * 2U + 1U],
                                   &ssd_layer_values_) ||
                !ssd_postprocess_.AppendConfidenceLayer(layer,
                                                        ssd_layer_values_)) {
                return false;
            }
        }

        return ssd_postprocess_.IsFrameComplete();
    }

    bool PrepareSsdPostprocess() {
        ssd_model_ready_ = IsSsdModel();
        if (!ssd_model_ready_) {
            ssd_layer_values_.clear();
            ssd_postprocess_.Clear();
            return true;
        }
        if (!ssd_postprocess_.Prepare()) {
            Error("ai", "Prepare SSD priors failed");
            ClearSsdPostprocessCache();
            return false;
        }
        ssd_layer_values_.reserve(kSsdSoftmaxInputChannel[0]);
        return true;
    }

    void ClearSsdPostprocessCache() {
        ssd_model_ready_ = false;
        ssd_layer_values_.clear();
        ssd_postprocess_.Clear();
    }

    std::vector<AiDetection> DecodeSsdDetections(const AiModelConfig &config) {
        if (!CollectSsdOutputs()) {
            return std::vector<AiDetection>();
        }
        return ssd_postprocess_.DecodeDetections(config);
    }

    NnieModelSession model_session_;
    SVP_MEM_INFO_S workspace_buf_{};
    SVP_MEM_INFO_S tmp_buf_{};
    HI_U32 task_buf_sizes_[SVP_NNIE_MAX_NET_SEG_NUM]{};
    HI_U32 tmp_buf_size_ = 0;
    NnieBlobSize blob_sizes_[SVP_NNIE_MAX_NET_SEG_NUM]{};
    NnieSegData seg_data_[SVP_NNIE_MAX_NET_SEG_NUM]{};
    SVP_NNIE_FORWARD_CTRL_S forward_ctrl_[SVP_NNIE_MAX_NET_SEG_NUM]{};
    NnieInputWriter input_writer_;
    std::vector<int32_t> ssd_layer_values_;
    SsdPostprocess ssd_postprocess_;
    bool ssd_model_ready_ = false;
#endif
    bool started_ = false;
};

NnieObjectBackend::NnieObjectBackend() : impl_(new Impl()) {}

NnieObjectBackend::~NnieObjectBackend() { delete impl_; }

bool NnieObjectBackend::Start(const AiModelConfig &config) {
    return impl_->Start(config);
}

void NnieObjectBackend::Stop() { impl_->Stop(); }

bool NnieObjectBackend::Started() const { return impl_->Started(); }

AiInferenceResult NnieObjectBackend::Run(const hisisdk::YuvFrame &frame,
                                         StreamId stream_id,
                                         const AiModelConfig &config) {
    return impl_->Run(frame, stream_id, config);
}

}  // namespace ai_internal
}  // namespace live_stream
