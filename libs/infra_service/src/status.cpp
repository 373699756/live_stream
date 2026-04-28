#include "infra/status.h"

namespace infra {

const char* StatusToString(Status status) {
    switch (status) {
        case Status::kOk:
            return "Ok";
        case Status::kInvalidParam:
            return "InvalidParam";
        case Status::kNotFound:
            return "NotFound";
        case Status::kAlreadyExists:
            return "AlreadyExists";
        case Status::kNoPermission:
            return "NoPermission";
        case Status::kUnauthorized:
            return "Unauthorized";
        case Status::kTimeout:
            return "Timeout";
        case Status::kBusy:
            return "Busy";
        case Status::kNoMemory:
            return "NoMemory";
        case Status::kNotSupported:
            return "NotSupported";
        case Status::kIoError:
            return "IoError";
        case Status::kInternalError:
            return "InternalError";
    }

    return "Unknown";
}

}  // namespace infra
