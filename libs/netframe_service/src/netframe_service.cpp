#include "netframe_service.h"

#include "net_engine_impl.h"

#include <memory>
#include <utility>

namespace live_stream {

infra::Result<std::unique_ptr<NetEngine>> CreateNetEngine(
    const NetEngineOptions& options) {
  if (options.io_threads == 0 ||
      (options.callback_mode == CallbackMode::kPostToExecutor &&
       options.callback_executor == nullptr)) {
    return infra::Result<std::unique_ptr<NetEngine>>::Fail(
        infra::Status::kInvalidParam);
  }
  std::unique_ptr<NetEngine> engine(
      new netframe_internal::NetEngineImpl(options));
  return infra::Result<std::unique_ptr<NetEngine>>::Ok(std::move(engine));
}

}  // namespace live_stream
