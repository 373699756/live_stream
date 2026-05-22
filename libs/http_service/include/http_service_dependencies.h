#ifndef LIVE_STREAM_HTTP_SERVICE_DEPENDENCIES_H_
#define LIVE_STREAM_HTTP_SERVICE_DEPENDENCIES_H_

#include "http_service.h"

namespace live_stream {

class NetEngine;

struct HttpServiceDependencies {
    NetEngine *net_engine = nullptr;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SERVICE_DEPENDENCIES_H_
