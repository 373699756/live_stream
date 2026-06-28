#ifndef LIVE_STREAM_AI_SRC_NNIE_FORWARD_WORKSPACE_H_
#define LIVE_STREAM_AI_SRC_NNIE_FORWARD_WORKSPACE_H_

#include "ai.h"
#include "hisi_ai_platform.h"

namespace live_stream {
namespace ai_internal {

class NnieForwardWorkspace {
public:
    NnieForwardWorkspace();
    NnieForwardWorkspace(const NnieForwardWorkspace &) = delete;
    NnieForwardWorkspace &operator=(const NnieForwardWorkspace &) = delete;
    ~NnieForwardWorkspace();

#if LIVE_STREAM_HAS_HISI_NNIE
    bool Prepare(SVP_NNIE_MODEL_S *model);
    void Release();
    bool ValidateInputConfig(const AiModelConfig &config) const;
    bool IsSsdModel(const SVP_NNIE_MODEL_S &model) const;
    bool Forward(SVP_NNIE_MODEL_S *model);
    SVP_SRC_BLOB_S *InputBlob();
    const SVP_DST_BLOB_S &OutputBlob(uint32_t index) const;
#endif

private:
#if LIVE_STREAM_HAS_HISI_NNIE
    bool ValidateForwardConfig(const SVP_NNIE_MODEL_S &model) const;
    bool FillForwardInfo(const SVP_NNIE_MODEL_S &model);
    bool AddBlobSizes(const SVP_NNIE_MODEL_S &model,
                      HI_U32 *total_workspace_size);
    void FillWorkspaceAddresses(const SVP_NNIE_MODEL_S &model,
                                HI_U32 total_task_size,
                                HI_U32 tmp_buf_size);
#endif

    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_NNIE_FORWARD_WORKSPACE_H_
