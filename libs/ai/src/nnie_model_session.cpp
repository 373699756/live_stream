#include "nnie_model_session.h"

#include "ai_model_paths.h"
#include "infra/fs.h"
#include "infra/log.h"

#include <cstdio>
#include <cstring>

namespace live_stream {
namespace ai_internal {
namespace {

#if LIVE_STREAM_HAS_HISI_NNIE
bool IsSupportedCnnNode(const SVP_NNIE_NODE_S &node) {
    if (node.enType == SVP_BLOB_TYPE_SEQ_S32) {
        return false;
    }
    return node.unShape.stWhc.u32Width != 0 &&
           node.unShape.stWhc.u32Height != 0 &&
           node.unShape.stWhc.u32Chn != 0;
}
#endif

}  // namespace

NnieModelSession::~NnieModelSession() { Unload(); }

bool NnieModelSession::Load(const AiModelConfig &config) {
#if LIVE_STREAM_HAS_HISI_NNIE
    Unload();
    const std::string &model_path = config.model_path;
    const std::string resolved_model_path = ResolveAiModelPath(model_path);
    const uint64_t model_file_size = infra::File::Size(resolved_model_path);
    if (model_file_size == 0 || model_file_size > kMaxHiU32) {
        Error("ai", "Invalid NNIE model size: path=%s resolved=%s size=%llu",
              model_path.c_str(), resolved_model_path.c_str(),
              static_cast<unsigned long long>(model_file_size));
        return false;
    }

    std::FILE *model_file = std::fopen(resolved_model_path.c_str(), "rb");
    if (model_file == nullptr) {
        Error("ai", "Open NNIE model failed: path=%s resolved=%s",
              model_path.c_str(), resolved_model_path.c_str());
        return false;
    }

    HI_U64 model_phy_addr = 0;
    HI_VOID *model_vir_addr = nullptr;
    const HI_U32 model_size = static_cast<HI_U32>(model_file_size);
    HI_S32 ret = HI_MPI_SYS_MmzAlloc(&model_phy_addr, &model_vir_addr,
                                     "LIVE_AI_NNIE_MODEL", nullptr,
                                     model_size);
    if (ret != HI_SUCCESS || model_phy_addr == 0 ||
        model_vir_addr == nullptr) {
        Error("ai", "Allocate NNIE model MMZ failed: ret=%#x",
              static_cast<unsigned int>(ret));
        std::fclose(model_file);
        return false;
    }

    const size_t read_size =
        std::fread(model_vir_addr, 1, model_size, model_file);
    const int close_status = std::fclose(model_file);
    if (read_size != static_cast<size_t>(model_size) ||
        close_status != 0) {
        Error("ai", "Read NNIE model failed: path=%s resolved=%s read=%u size=%u",
              model_path.c_str(), resolved_model_path.c_str(),
              static_cast<unsigned int>(read_size),
              static_cast<unsigned int>(model_size));
        HI_MPI_SYS_MmzFree(model_phy_addr, model_vir_addr);
        return false;
    }
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

    loaded_ = true;
    if (!ValidateModel()) {
        Unload();
        return false;
    }
    Info("ai", "NNIE model loaded task=%d path=%s size=%u segs=%u tmp=%u",
         static_cast<int>(config.task), resolved_model_path.c_str(),
         static_cast<unsigned int>(model_size),
         static_cast<unsigned int>(model_.u32NetSegNum),
         static_cast<unsigned int>(model_.u32TmpBufSize));
    return true;
#else
    (void)config;
    return false;
#endif
}

void NnieModelSession::Unload() {
#if LIVE_STREAM_HAS_HISI_NNIE
    if (loaded_) {
        const HI_S32 ret = HI_MPI_SVP_NNIE_UnloadModel(&model_);
        if (ret != HI_SUCCESS) {
            Error("ai", "Unload NNIE model failed: ret=%#x",
                  static_cast<unsigned int>(ret));
        }
        loaded_ = false;
    }

    if (model_buf_.u64PhyAddr != 0 && model_buf_.u64VirAddr != 0) {
        HI_MPI_SYS_MmzFree(
            model_buf_.u64PhyAddr,
            reinterpret_cast<HI_VOID *>(
                static_cast<HI_UL>(model_buf_.u64VirAddr)));
    }
    std::memset(&model_buf_, 0, sizeof(model_buf_));
    std::memset(&model_, 0, sizeof(model_));
#endif
}

#if LIVE_STREAM_HAS_HISI_NNIE
bool NnieModelSession::ValidateModel() const {
    if (model_.u32NetSegNum == 0 ||
        model_.u32NetSegNum > SVP_NNIE_MAX_NET_SEG_NUM) {
        Error("ai", "Invalid NNIE segment size: size=%u",
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
#endif

bool NnieModelSession::Loaded() const { return loaded_; }

#if LIVE_STREAM_HAS_HISI_NNIE
const SVP_NNIE_MODEL_S &NnieModelSession::Model() const { return model_; }

SVP_NNIE_MODEL_S *NnieModelSession::MutableModel() { return &model_; }
#endif

}  // namespace ai_internal
}  // namespace live_stream
