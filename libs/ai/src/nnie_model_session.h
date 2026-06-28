#ifndef LIVE_STREAM_AI_SRC_NNIE_MODEL_SESSION_H_
#define LIVE_STREAM_AI_SRC_NNIE_MODEL_SESSION_H_

#include "ai.h"
#include "hisi_ai_platform.h"

namespace live_stream {
namespace ai_internal {

class NnieModelSession {
public:
    NnieModelSession() = default;
    NnieModelSession(const NnieModelSession &) = delete;
    NnieModelSession &operator=(const NnieModelSession &) = delete;
    ~NnieModelSession();

    bool Load(const AiModelConfig &config);
    void Unload();
    bool Loaded() const;

#if LIVE_STREAM_HAS_HISI_NNIE
    const SVP_NNIE_MODEL_S &Model() const;
    SVP_NNIE_MODEL_S *MutableModel();
#endif

private:
#if LIVE_STREAM_HAS_HISI_NNIE
    bool ValidateModel() const;

    SVP_SRC_MEM_INFO_S model_buf_{};
    SVP_NNIE_MODEL_S model_{};
#endif
    bool loaded_ = false;
};

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_NNIE_MODEL_SESSION_H_
