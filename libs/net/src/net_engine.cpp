#include "net.h"

#include "net_engine_impl.h"

#include <memory>
#include <utility>

namespace live_stream {

const char *TcpCloseReasonName(TcpCloseReason reason) {
    switch (reason) {
        case TcpCloseReason::kNormal:
            return "normal";
        case TcpCloseReason::kRemoteClose:
            return "remote_close";
        case TcpCloseReason::kParseError:
            return "parse_error";
        case TcpCloseReason::kAuthFailed:
            return "auth_failed";
        case TcpCloseReason::kQueueFull:
            return "queue_full";
        case TcpCloseReason::kPendingLimit:
            return "pending_limit";
        case TcpCloseReason::kSendStall:
            return "send_stall";
        case TcpCloseReason::kReadTimeout:
            return "read_timeout";
        case TcpCloseReason::kWriteTimeout:
            return "write_timeout";
        case TcpCloseReason::kInternalError:
            return "internal_error";
    }
    return "internal_error";
}

std::unique_ptr<INetEngine> CreateNetEngine(const NetEngineOptions &options) {
    if (options.io_threads == 0 ||
        (options.callback_mode == CallbackMode::kPostToExecutor &&
         options.callback_executor == nullptr)) {
        return nullptr;
    }
    return std::unique_ptr<INetEngine>(new net_internal::NetEngineImpl(options));
}

}  // namespace live_stream
