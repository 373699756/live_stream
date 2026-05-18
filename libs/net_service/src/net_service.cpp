#include "net_service.h"

#include "net_engine_impl.h"

#include <memory>
#include <utility>

namespace live_stream {

std::unique_ptr<NetEngine> CreateNetEngine(const NetEngineOptions &options) {
    if (options.io_threads == 0 ||
        (options.callback_mode == CallbackMode::kPostToExecutor &&
         options.callback_executor == nullptr)) {
        return nullptr;
    }
    return std::unique_ptr<NetEngine>(new net_internal::NetEngineImpl(options));
}

}  // namespace live_stream
