#ifndef LIVE_STREAM_INFRA_ERRNO_UTIL_H_
#define LIVE_STREAM_INFRA_ERRNO_UTIL_H_

#include "infra/status.h"

namespace infra {

Status ErrnoToStatus(int error);

}  // namespace infra

#endif  // LIVE_STREAM_INFRA_ERRNO_UTIL_H_
