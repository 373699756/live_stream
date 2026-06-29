#ifndef LIVE_STREAM_HTTP_SRC_HANDLERS_SYSTEM_OVERVIEW_RESPONSE_H_
#define LIVE_STREAM_HTTP_SRC_HANDLERS_SYSTEM_OVERVIEW_RESPONSE_H_

#include "handlers/http_handlers.h"
#include "json.h"

namespace live_stream {

Json BuildSystemOverviewJson(ISystem *system,
                             const SystemOverviewSources &sources);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HANDLERS_SYSTEM_OVERVIEW_RESPONSE_H_
