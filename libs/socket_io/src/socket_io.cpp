#include "socket_io.h"

#include "socket_io_impl.h"

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

std::unique_ptr<ISocketIo> CreateSocketIo(const SocketIoOptions &options) {
    if (options.io_threads == 0 ||
        (options.callback_mode == CallbackMode::kPostToLoop &&
         options.callback_loop == nullptr)) {
        return nullptr;
    }
    return std::unique_ptr<ISocketIo>(new socket_io_internal::SocketIoImpl(options));
}

}  // namespace live_stream
