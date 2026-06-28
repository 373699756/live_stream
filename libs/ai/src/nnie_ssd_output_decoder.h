#ifndef LIVE_STREAM_AI_SRC_NNIE_SSD_OUTPUT_DECODER_H_
#define LIVE_STREAM_AI_SRC_NNIE_SSD_OUTPUT_DECODER_H_

#include "ai.h"
#include "hisi_ai_platform.h"
#include "nnie_forward_workspace.h"
#include "ssd_postprocess.h"

#include <vector>

namespace live_stream {
namespace ai_internal {

#if LIVE_STREAM_HAS_HISI_NNIE
class NnieSsdOutputDecoder {
public:
    bool Prepare(const SVP_NNIE_MODEL_S &model,
                 const NnieForwardWorkspace &workspace);
    void Clear();
    bool Ready() const;
    std::vector<AiDetection> Decode(const NnieForwardWorkspace &workspace,
                                    const AiModelConfig &config);

private:
    bool CopyS32BlobValues(const SVP_DST_BLOB_S &blob,
                           std::vector<int32_t> *values) const;
    bool CollectOutputs(const NnieForwardWorkspace &workspace);

    std::vector<int32_t> layer_values_;
    SsdPostprocess postprocess_;
    bool ready_ = false;
};
#endif

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_NNIE_SSD_OUTPUT_DECODER_H_
