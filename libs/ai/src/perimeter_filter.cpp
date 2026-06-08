#include "perimeter_filter.h"

#include <string>

namespace live_stream {
namespace ai_internal {
namespace {

bool IsPerimeterTargetLabel(const std::string &label) {
    return label == "person" || label == "car" || label == "bus" ||
           label == "truck" || label == "motorbike" || label == "bicycle" ||
           label == "vehicle";
}

bool DetectionCenterInsideRegion(const AiDetection &detection,
                                 const AiPerimeterRegion &region) {
    const float center_x = detection.x + detection.width * 0.5f;
    const float center_y = detection.y + detection.height * 0.5f;
    return center_x >= region.x && center_x <= region.x + region.width &&
           center_y >= region.y && center_y <= region.y + region.height;
}

bool DetectionInsidePerimeter(const AiDetection &detection,
                              const AiPerimeterConfig &perimeter) {
    if (perimeter.regions.empty()) {
        return true;
    }
    for (const AiPerimeterRegion &region : perimeter.regions) {
        if (DetectionCenterInsideRegion(detection, region)) {
            return true;
        }
    }
    return false;
}

}  // namespace

AiInferenceResult FilterPerimeterDetections(
    const AiInferenceResult &result, const AiPerimeterConfig &perimeter) {
    AiInferenceResult filtered = result;
    filtered.detections.clear();
    for (const AiDetection &detection : result.detections) {
        if (IsPerimeterTargetLabel(detection.label) &&
            DetectionInsidePerimeter(detection, perimeter)) {
            filtered.detections.push_back(detection);
        }
    }
    return filtered;
}

}  // namespace ai_internal
}  // namespace live_stream
