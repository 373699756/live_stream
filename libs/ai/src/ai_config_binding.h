#ifndef LIVE_STREAM_AI_SRC_AI_CONFIG_BINDING_H_
#define LIVE_STREAM_AI_SRC_AI_CONFIG_BINDING_H_

#include "ai.h"
#include "config.h"

#include <functional>

namespace live_stream {
namespace ai_internal {

class AiConfigBinding final {
public:
    using CurrentConfigReader = std::function<AiConfig()>;
    using ConfigApplier = std::function<bool(const AiConfig &)>;

    explicit AiConfigBinding(IConfig *config);
    AiConfigBinding(const AiConfigBinding &) = delete;
    AiConfigBinding &operator=(const AiConfigBinding &) = delete;
    ~AiConfigBinding();

    bool Attach(const CurrentConfigReader &read_current_config,
                const ConfigApplier &apply_config);
    void Detach();
    bool LoadInitial(const AiConfig &current_config,
                     AiConfig *loaded_config) const;

private:
    IConfig *config_ = nullptr;
    bool attached_ = false;
};

}  // namespace ai_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_AI_SRC_AI_CONFIG_BINDING_H_
