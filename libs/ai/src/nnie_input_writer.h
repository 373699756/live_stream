#ifndef LIVE_STREAM_AI_SRC_NNIE_INPUT_WRITER_H_
#define LIVE_STREAM_AI_SRC_NNIE_INPUT_WRITER_H_

#include "ai.h"
#include "hisi_ai_platform.h"
#include "hisi_vendor/sdk.h"

namespace live_stream {
namespace ai_internal {

class NnieInputWriter {
public:
    NnieInputWriter();
    NnieInputWriter(const NnieInputWriter &) = delete;
    NnieInputWriter &operator=(const NnieInputWriter &) = delete;
    ~NnieInputWriter();

#if LIVE_STREAM_HAS_HISI_NNIE
    bool Write(const hisisdk::YuvFrame &frame,
               const AiModelConfig &config,
               SVP_SRC_BLOB_S *input_tensor);
#else
    bool Write(const hisisdk::YuvFrame &frame,
               const AiModelConfig &config,
               void *input_tensor);
#endif
    void Release();

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_NNIE_INPUT_WRITER_H_
