#include "nnie_forward_workspace.h"

#include "infra/log.h"

#include <cstring>

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

struct NnieForwardWorkspace::Impl {
#if LIVE_STREAM_HAS_HISI_NNIE
    SVP_MEM_INFO_S workspace_buf{};
    SVP_MEM_INFO_S tmp_buf{};
    HI_U32 task_buf_sizes[SVP_NNIE_MAX_NET_SEG_NUM]{};
    HI_U32 tmp_buf_size = 0;
    NnieBlobSize blob_sizes[SVP_NNIE_MAX_NET_SEG_NUM]{};
    NnieSegData seg_data[SVP_NNIE_MAX_NET_SEG_NUM]{};
    SVP_NNIE_FORWARD_CTRL_S forward_ctrl[SVP_NNIE_MAX_NET_SEG_NUM]{};
#endif
};

NnieForwardWorkspace::NnieForwardWorkspace() : impl_(new Impl()) {}

NnieForwardWorkspace::~NnieForwardWorkspace() {
#if LIVE_STREAM_HAS_HISI_NNIE
    Release();
#endif
    delete impl_;
}

#if LIVE_STREAM_HAS_HISI_NNIE
bool NnieForwardWorkspace::Prepare(SVP_NNIE_MODEL_S *model) {
    if (model == nullptr) {
        return false;
    }
    Info("ai", "NNIE workspace prepare begin: segs=%u",
         static_cast<unsigned int>(model->u32NetSegNum));
    if (!FillForwardInfo(*model)) {
        return false;
    }

    HI_U32 total_task_size = 0;
    HI_U32 total_workspace_size = 0;
    HI_S32 ret = HI_MPI_SVP_NNIE_GetTskBufSize(
        kNnieMaxInputNum, 0, model, impl_->task_buf_sizes,
        model->u32NetSegNum);
    if (ret != HI_SUCCESS) {
        Error("ai", "Get NNIE task buffer size failed: ret=%#x",
              static_cast<unsigned int>(ret));
        return false;
    }
    for (HI_U32 i = 0; i < model->u32NetSegNum; ++i) {
        if (!AddHiU32(impl_->task_buf_sizes[i], &total_task_size)) {
            return false;
        }
    }
    impl_->tmp_buf_size = model->u32TmpBufSize;
    Info("ai", "NNIE task buffers ready: total_task=%u tmp=%u",
         static_cast<unsigned int>(total_task_size),
         static_cast<unsigned int>(impl_->tmp_buf_size));
    if (!AddHiU32(total_task_size, &total_workspace_size) ||
        !AddHiU32(impl_->tmp_buf_size, &total_workspace_size) ||
        !AddBlobSizes(*model, &total_workspace_size)) {
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

    impl_->workspace_buf.u32Size = total_workspace_size;
    impl_->workspace_buf.u64PhyAddr = workspace_phy_addr;
    impl_->workspace_buf.u64VirAddr =
        static_cast<HI_U64>(reinterpret_cast<HI_UL>(workspace_vir_addr));
    FillWorkspaceAddresses(*model, total_task_size, impl_->tmp_buf_size);
    return true;
}

bool NnieForwardWorkspace::ValidateForwardConfig(
    const SVP_NNIE_MODEL_S &model) const {
    if (model.u32NetSegNum != 1) {
        Error("ai", "Unsupported NNIE forward segment size: size=%u",
              static_cast<unsigned int>(model.u32NetSegNum));
        return false;
    }
    return true;
}

bool NnieForwardWorkspace::ValidateInputConfig(
    const AiModelConfig &config) const {
    const SVP_SRC_BLOB_S &src = impl_->seg_data[0].src[0];
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

bool NnieForwardWorkspace::IsSsdModel(const SVP_NNIE_MODEL_S &model) const {
    if (model->u32NetSegNum != 1) {
        return false;
    }
    const SVP_NNIE_SEG_S &seg = model.astSeg[0];
    const SVP_SRC_BLOB_S &src = impl_->seg_data[0].src[0];
    if (seg.u16SrcNum != 1 || seg.u16DstNum != kSsdReportNodes ||
        src.enType != SVP_BLOB_TYPE_U8 ||
        src.unShape.stWhc.u32Chn != 3 ||
        src.unShape.stWhc.u32Width != kSsdInputWidth ||
        src.unShape.stWhc.u32Height != kSsdInputHeight) {
        return false;
    }
    for (uint32_t i = 0; i < kSsdReportNodes; ++i) {
        if (impl_->seg_data[0].dst[i].enType != SVP_BLOB_TYPE_S32) {
            return false;
        }
    }
    return true;
}

bool NnieForwardWorkspace::FillForwardInfo(const SVP_NNIE_MODEL_S &model) {
    std::memset(impl_->seg_data, 0, sizeof(impl_->seg_data));
    std::memset(impl_->forward_ctrl, 0, sizeof(impl_->forward_ctrl));
    for (HI_U32 i = 0; i < model->u32NetSegNum; ++i) {
        const SVP_NNIE_SEG_S &seg = model.astSeg[i];
        impl_->forward_ctrl[i].enNnieId = SVP_NNIE_ID_0;
        impl_->forward_ctrl[i].u32SrcNum = seg.u16SrcNum;
        impl_->forward_ctrl[i].u32DstNum = seg.u16DstNum;
        impl_->forward_ctrl[i].u32NetSegId = i;

        for (HI_U32 j = 0; j < seg.u16SrcNum; ++j) {
            impl_->seg_data[i].src[j].enType = seg.astSrcNode[j].enType;
            impl_->seg_data[i].src[j].u32Num = kNnieMaxInputNum;
            impl_->seg_data[i].src[j].unShape.stWhc.u32Chn =
                seg.astSrcNode[j].unShape.stWhc.u32Chn;
            impl_->seg_data[i].src[j].unShape.stWhc.u32Height =
                seg.astSrcNode[j].unShape.stWhc.u32Height;
            impl_->seg_data[i].src[j].unShape.stWhc.u32Width =
                seg.astSrcNode[j].unShape.stWhc.u32Width;
        }
        for (HI_U32 j = 0; j < seg.u16DstNum; ++j) {
            impl_->seg_data[i].dst[j].enType = seg.astDstNode[j].enType;
            impl_->seg_data[i].dst[j].u32Num = kNnieMaxInputNum;
            impl_->seg_data[i].dst[j].unShape.stWhc.u32Chn =
                seg.astDstNode[j].unShape.stWhc.u32Chn;
            impl_->seg_data[i].dst[j].unShape.stWhc.u32Height =
                seg.astDstNode[j].unShape.stWhc.u32Height;
            impl_->seg_data[i].dst[j].unShape.stWhc.u32Width =
                seg.astDstNode[j].unShape.stWhc.u32Width;
        }
    }
    return true;
}

bool NnieForwardWorkspace::AddBlobSizes(
    const SVP_NNIE_MODEL_S &model,
    HI_U32 *total_workspace_size) {
    if (total_workspace_size == nullptr) {
        return false;
    }
    std::memset(impl_->blob_sizes, 0, sizeof(impl_->blob_sizes));
    for (HI_U32 i = 0; i < model->u32NetSegNum; ++i) {
        const SVP_NNIE_SEG_S &seg = model.astSeg[i];
        if (i == 0) {
            for (HI_U32 j = 0; j < seg.u16SrcNum; ++j) {
                if (!ComputeBlobSize(seg.astSrcNode[j], 0,
                                     &impl_->seg_data[i].src[j],
                                     &impl_->blob_sizes[i].src[j]) ||
                    !AddHiU32(impl_->blob_sizes[i].src[j],
                              total_workspace_size)) {
                    return false;
                }
            }
        }
        for (HI_U32 j = 0; j < seg.u16DstNum; ++j) {
            if (!ComputeBlobSize(seg.astDstNode[j], 0,
                                 &impl_->seg_data[i].dst[j],
                                 &impl_->blob_sizes[i].dst[j]) ||
                !AddHiU32(impl_->blob_sizes[i].dst[j],
                          total_workspace_size)) {
                return false;
            }
        }
    }
    return true;
}

void NnieForwardWorkspace::FillWorkspaceAddresses(
    const SVP_NNIE_MODEL_S &model,
    HI_U32 total_task_size,
    HI_U32 tmp_buf_size) {
    SVP_MEM_INFO_S task_buf;
    task_buf.u32Size = total_task_size;
    task_buf.u64PhyAddr = impl_->workspace_buf.u64PhyAddr;
    task_buf.u64VirAddr = impl_->workspace_buf.u64VirAddr;

    impl_->tmp_buf.u32Size = tmp_buf_size;
    impl_->tmp_buf.u64PhyAddr =
        impl_->workspace_buf.u64PhyAddr + total_task_size;
    impl_->tmp_buf.u64VirAddr =
        impl_->workspace_buf.u64VirAddr + total_task_size;

    HI_U32 task_offset = 0;
    for (HI_U32 i = 0; i < model->u32NetSegNum; ++i) {
        impl_->forward_ctrl[i].stTmpBuf = impl_->tmp_buf;
        impl_->forward_ctrl[i].stTskBuf.u32Size = impl_->task_buf_sizes[i];
        impl_->forward_ctrl[i].stTskBuf.u64PhyAddr =
            task_buf.u64PhyAddr + task_offset;
        impl_->forward_ctrl[i].stTskBuf.u64VirAddr =
            task_buf.u64VirAddr + task_offset;
        task_offset += impl_->task_buf_sizes[i];
    }

    HI_U64 current_phy_addr =
        impl_->workspace_buf.u64PhyAddr + total_task_size + tmp_buf_size;
    HI_U64 current_vir_addr =
        impl_->workspace_buf.u64VirAddr + total_task_size + tmp_buf_size;
    for (HI_U32 i = 0; i < model->u32NetSegNum; ++i) {
        const SVP_NNIE_SEG_S &seg = model.astSeg[i];
        if (i == 0) {
            for (HI_U32 j = 0; j < seg.u16SrcNum; ++j) {
                impl_->seg_data[i].src[j].u64PhyAddr = current_phy_addr;
                impl_->seg_data[i].src[j].u64VirAddr = current_vir_addr;
                current_phy_addr += impl_->blob_sizes[i].src[j];
                current_vir_addr += impl_->blob_sizes[i].src[j];
            }
        }
        for (HI_U32 j = 0; j < seg.u16DstNum; ++j) {
            impl_->seg_data[i].dst[j].u64PhyAddr = current_phy_addr;
            impl_->seg_data[i].dst[j].u64VirAddr = current_vir_addr;
            current_phy_addr += impl_->blob_sizes[i].dst[j];
            current_vir_addr += impl_->blob_sizes[i].dst[j];
        }
    }
}

void NnieForwardWorkspace::Release() {
    if (impl_->workspace_buf.u64PhyAddr != 0 &&
        impl_->workspace_buf.u64VirAddr != 0) {
        HI_MPI_SYS_MmzFree(impl_->workspace_buf.u64PhyAddr,
                           VirAddrToPointer(impl_->workspace_buf.u64VirAddr));
    }
    std::memset(&impl_->workspace_buf, 0, sizeof(impl_->workspace_buf));
    std::memset(&impl_->tmp_buf, 0, sizeof(impl_->tmp_buf));
    std::memset(impl_->task_buf_sizes, 0, sizeof(impl_->task_buf_sizes));
    std::memset(impl_->blob_sizes, 0, sizeof(impl_->blob_sizes));
    std::memset(impl_->seg_data, 0, sizeof(impl_->seg_data));
    std::memset(impl_->forward_ctrl, 0, sizeof(impl_->forward_ctrl));
    impl_->tmp_buf_size = 0;
}

bool NnieForwardWorkspace::Forward(SVP_NNIE_MODEL_S *model) {
    if (model == nullptr || !ValidateForwardConfig(*model)) {
        return false;
    }
    SVP_NNIE_FORWARD_CTRL_S &ctrl = impl_->forward_ctrl[0];
    if (HI_MPI_SYS_MmzFlushCache(ctrl.stTskBuf.u64PhyAddr,
                                 VirAddrToPointer(ctrl.stTskBuf.u64VirAddr),
                                 ctrl.stTskBuf.u32Size) != HI_SUCCESS) {
        return false;
    }
    for (HI_U32 i = 0; i < ctrl.u32DstNum; ++i) {
        if (!FlushBlob(impl_->seg_data[0].dst[i])) {
            return false;
        }
    }

    SVP_NNIE_HANDLE handle = 0;
    HI_S32 ret = HI_MPI_SVP_NNIE_Forward(&handle, impl_->seg_data[0].src,
                                         model, impl_->seg_data[0].dst,
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
        if (!FlushBlob(impl_->seg_data[0].dst[i])) {
            return false;
        }
    }
    return true;
}

SVP_SRC_BLOB_S *NnieForwardWorkspace::InputBlob() {
    return &impl_->seg_data[0].src[0];
}

const SVP_DST_BLOB_S &NnieForwardWorkspace::OutputBlob(uint32_t index) const {
    return impl_->seg_data[0].dst[index];
}
#endif

}  // namespace ai_internal
}  // namespace live_stream
