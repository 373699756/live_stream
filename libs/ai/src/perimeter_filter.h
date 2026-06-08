#ifndef LIVE_STREAM_AI_SRC_PERIMETER_FILTER_H_
#define LIVE_STREAM_AI_SRC_PERIMETER_FILTER_H_

#include "ai.h"

namespace live_stream {
namespace ai_internal {

AiInferenceResult FilterPerimeterDetections(
    const AiInferenceResult &result, const AiPerimeterConfig &perimeter);

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_PERIMETER_FILTER_H_
