#ifndef LIVE_STREAM_AI_SRC_NNIE_OBJECT_BACKEND_H_
#define LIVE_STREAM_AI_SRC_NNIE_OBJECT_BACKEND_H_

#include "ai.h"
#include "hisi_vendor/sdk.h"

namespace live_stream {
namespace ai_internal {

class NnieObjectBackend {
public:
    NnieObjectBackend();
    NnieObjectBackend(const NnieObjectBackend &) = delete;
    NnieObjectBackend &operator=(const NnieObjectBackend &) = delete;
    ~NnieObjectBackend();

    bool Start(const AiModelConfig &config);
    void Stop();
    bool Started() const;
    AiInferenceResult Run(const hisisdk::YuvFrame &frame,
                          StreamId stream_id,
                          const AiModelConfig &config);

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_NNIE_OBJECT_BACKEND_H_
