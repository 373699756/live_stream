#include "ssd_postprocess.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace live_stream {
namespace ai_internal {
namespace {

constexpr uint32_t kSsdTopK = 400;
constexpr uint32_t kSsdKeepTopK = 200;
constexpr int32_t kSsdQuantBase = 4096;
constexpr float kSsdNmsThreshold = 0.3f;

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
constexpr std::array<const char *, kSsdClassCount> kSsdVocLabels = {
    {"background", "aeroplane", "bicycle", "bird", "boat", "bottle", "bus",
     "car", "cat", "chair", "cow", "diningtable", "dog", "horse",
     "motorbike", "person", "pottedplant", "sheep", "sofa", "train",
     "tvmonitor"}};

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
                min_prior.x_min = static_cast<float>(
                    static_cast<int32_t>(center_x - min_size * 0.5f));
                min_prior.y_min = static_cast<float>(
                    static_cast<int32_t>(center_y - min_size * 0.5f));
                min_prior.x_max = static_cast<float>(
                    static_cast<int32_t>(center_x + min_size * 0.5f));
                min_prior.y_max = static_cast<float>(
                    static_cast<int32_t>(center_y + min_size * 0.5f));
                for (uint32_t i = 0; i < kSsdCoordinateCount; ++i) {
                    min_prior.variance[i] =
                        QuantizedConfidence(kSsdPriorVariance[i]);
                }
                priors.push_back(min_prior);

                const float max_size =
                    std::sqrt(min_size * kSsdPriorMaxSize[layer]);
                SsdPrior max_prior;
                max_prior.x_min = static_cast<float>(
                    static_cast<int32_t>(center_x - max_size * 0.5f));
                max_prior.y_min = static_cast<float>(
                    static_cast<int32_t>(center_y - max_size * 0.5f));
                max_prior.x_max = static_cast<float>(
                    static_cast<int32_t>(center_x + max_size * 0.5f));
                max_prior.y_max = static_cast<float>(
                    static_cast<int32_t>(center_y + max_size * 0.5f));
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

}  // namespace

bool SsdPostprocess::Prepare() {
    Clear();
    priors_ = GenerateSsdPriors();
    if (priors_.size() != kSsdPriorCount) {
        Clear();
        return false;
    }
    loc_predictions_.reserve(kSsdPriorCount * kSsdCoordinateCount);
    conf_scores_.reserve(kSsdPriorCount * kSsdClassCount);
    boxes_.reserve(kSsdPriorCount);
    class_proposals_.reserve(kSsdPriorCount);
    proposals_after_nms_.reserve((kSsdClassCount - 1U) * kSsdTopK);
    nms_suppressed_.reserve(kSsdTopK);
    return true;
}

void SsdPostprocess::Clear() {
    priors_.clear();
    loc_predictions_.clear();
    conf_scores_.clear();
    boxes_.clear();
    class_proposals_.clear();
    proposals_after_nms_.clear();
    nms_suppressed_.clear();
}

void SsdPostprocess::BeginFrame() {
    loc_predictions_.clear();
    conf_scores_.clear();
    boxes_.clear();
    class_proposals_.clear();
    proposals_after_nms_.clear();
    nms_suppressed_.clear();
}

bool SsdPostprocess::AppendLocationLayer(
    uint32_t layer, const std::vector<int32_t> &values) {
    if (layer >= kSsdLayerCount ||
        values.size() != kSsdDetectInputChannel[layer]) {
        return false;
    }
    loc_predictions_.insert(loc_predictions_.end(), values.begin(),
                            values.end());
    return true;
}

bool SsdPostprocess::AppendConfidenceLayer(
    uint32_t layer, const std::vector<int32_t> &values) {
    if (layer >= kSsdLayerCount ||
        values.size() != kSsdSoftmaxInputChannel[layer] ||
        values.size() % kSsdClassCount != 0) {
        return false;
    }
    for (size_t offset = 0; offset < values.size(); offset += kSsdClassCount) {
        const size_t score_offset = conf_scores_.size();
        conf_scores_.resize(score_offset + kSsdClassCount);
        if (!SoftmaxQuantized(&values[offset], &conf_scores_[score_offset])) {
            return false;
        }
    }
    return true;
}

bool SsdPostprocess::IsFrameComplete() const {
    return priors_.size() == kSsdPriorCount &&
           loc_predictions_.size() == kSsdPriorCount * kSsdCoordinateCount &&
           conf_scores_.size() == kSsdPriorCount * kSsdClassCount;
}

std::vector<AiDetection> SsdPostprocess::DecodeDetections(
    const AiModelConfig &config) {
    if (!IsFrameComplete()) {
        return std::vector<AiDetection>();
    }
    if (!DecodeSsdBoxes(loc_predictions_, priors_, &boxes_)) {
        return std::vector<AiDetection>();
    }

    const int32_t score_threshold =
        QuantizeConfidence(config.confidence_threshold);
    proposals_after_nms_.clear();
    for (uint32_t class_id = 1; class_id < kSsdClassCount; ++class_id) {
        class_proposals_.clear();
        for (uint32_t i = 0; i < kSsdPriorCount; ++i) {
            const int32_t score =
                conf_scores_[i * kSsdClassCount + class_id];
            if (score < score_threshold || !IsValidSsdBox(boxes_[i])) {
                continue;
            }
            SsdProposal proposal;
            proposal.class_id = class_id;
            proposal.score = score;
            proposal.box = boxes_[i];
            class_proposals_.push_back(proposal);
        }
        AppendNmsProposals(&class_proposals_, &nms_suppressed_,
                           &proposals_after_nms_);
    }

    std::sort(proposals_after_nms_.begin(), proposals_after_nms_.end(),
              ProposalConfidenceGreater);
    if (proposals_after_nms_.size() > kSsdKeepTopK) {
        proposals_after_nms_.resize(kSsdKeepTopK);
    }
    if (proposals_after_nms_.size() > config.max_results) {
        proposals_after_nms_.resize(config.max_results);
    }

    std::vector<AiDetection> detections;
    detections.reserve(proposals_after_nms_.size());
    const float model_width = static_cast<float>(kSsdInputWidth);
    const float model_height = static_cast<float>(kSsdInputHeight);
    for (const SsdProposal &proposal : proposals_after_nms_) {
        const float x_min = ClampFloat(proposal.box.x_min, 0.0f, model_width);
        const float y_min = ClampFloat(proposal.box.y_min, 0.0f, model_height);
        const float x_max = ClampFloat(proposal.box.x_max, 0.0f, model_width);
        const float y_max = ClampFloat(proposal.box.y_max, 0.0f, model_height);
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

}  // namespace ai_internal
}  // namespace live_stream
