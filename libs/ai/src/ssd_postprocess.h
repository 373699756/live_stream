#ifndef LIVE_STREAM_AI_SRC_SSD_POSTPROCESS_H_
#define LIVE_STREAM_AI_SRC_SSD_POSTPROCESS_H_

#include "ai.h"

#include <array>
#include <cstdint>
#include <vector>

namespace live_stream {
namespace ai_internal {

constexpr uint32_t kSsdLayers = 6;
constexpr uint32_t kSsdReportNodes = 12;
constexpr uint32_t kSsdClasses = 21;
constexpr uint32_t kSsdInputWidth = 300;
constexpr uint32_t kSsdInputHeight = 300;
constexpr uint32_t kSsdPriors = 8732;
constexpr uint32_t kSsdBoxCoordinates = 4;

constexpr std::array<uint32_t, kSsdLayers> kSsdSoftmaxInputChannel = {
    {121296, 45486, 12600, 3150, 756, 84}};
constexpr std::array<uint32_t, kSsdLayers> kSsdDetectInputChannel = {
    {23104, 8664, 2400, 600, 144, 16}};

struct SsdPrior {
    float x_min = 0.0f;
    float y_min = 0.0f;
    float x_max = 0.0f;
    float y_max = 0.0f;
    std::array<float, kSsdBoxCoordinates> variance{};
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

class SsdPostprocess {
public:
    bool Prepare();
    void Clear();
    void BeginFrame();
    bool AppendLocationLayer(uint32_t layer,
                             const std::vector<int32_t> &values);
    bool AppendConfidenceLayer(uint32_t layer,
                               const std::vector<int32_t> &values);
    bool IsFrameComplete() const;
    std::vector<AiDetection> DecodeDetections(const AiModelConfig &config);

private:
    std::vector<SsdPrior> priors_;
    std::vector<int32_t> loc_predictions_;
    std::vector<int32_t> conf_scores_;
    std::vector<SsdDecodedBox> boxes_;
    std::vector<SsdProposal> class_proposals_;
    std::vector<SsdProposal> proposals_after_nms_;
    std::vector<uint8_t> nms_suppressed_;
};

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_SSD_POSTPROCESS_H_
