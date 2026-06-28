#include "nnie_ssd_output_decoder.h"

#include "infra/log.h"

#include <cstring>

namespace live_stream {
namespace ai_internal {

#if LIVE_STREAM_HAS_HISI_NNIE
bool NnieSsdOutputDecoder::Prepare(
    const SVP_NNIE_MODEL_S &model,
    const NnieForwardWorkspace &workspace) {
    if (!workspace.IsSsdModel(model)) {
        Clear();
        return true;
    }
    if (!postprocess_.Prepare()) {
        Error("ai", "Prepare SSD priors failed");
        Clear();
        return false;
    }
    layer_values_.clear();
    layer_values_.reserve(kSsdSoftmaxInputChannel[0]);
    ready_ = true;
    return true;
}

void NnieSsdOutputDecoder::Clear() {
    ready_ = false;
    layer_values_.clear();
    postprocess_.Clear();
}

bool NnieSsdOutputDecoder::Ready() const { return ready_; }

std::vector<AiDetection> NnieSsdOutputDecoder::Decode(
    const NnieForwardWorkspace &workspace,
    const AiModelConfig &config) {
    if (!ready_ || !CollectOutputs(workspace)) {
        return std::vector<AiDetection>();
    }
    return postprocess_.DecodeDetections(config);
}

bool NnieSsdOutputDecoder::CopyS32BlobValues(
    const SVP_DST_BLOB_S &blob,
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

bool NnieSsdOutputDecoder::CollectOutputs(
    const NnieForwardWorkspace &workspace) {
    postprocess_.BeginFrame();

    for (uint32_t layer = 0; layer < kSsdLayers; ++layer) {
        if (!CopyS32BlobValues(workspace.OutputBlob(layer * 2U),
                               &layer_values_) ||
            !postprocess_.AppendLocationLayer(layer, layer_values_)) {
            return false;
        }

        if (!CopyS32BlobValues(workspace.OutputBlob(layer * 2U + 1U),
                               &layer_values_) ||
            !postprocess_.AppendConfidenceLayer(layer, layer_values_)) {
            return false;
        }
    }

    return postprocess_.IsFrameComplete();
}
#endif

}  // namespace ai_internal
}  // namespace live_stream
