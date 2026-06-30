#ifndef LIVE_STREAM_AI_SRC_AI_MODEL_PATHS_H_
#define LIVE_STREAM_AI_SRC_AI_MODEL_PATHS_H_

#include <string>

namespace live_stream {
namespace ai_internal {

bool AiModelFileExists(const std::string &path);
std::string ResolveAiModelPath(const std::string &path);

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_MODEL_PATHS_H_
