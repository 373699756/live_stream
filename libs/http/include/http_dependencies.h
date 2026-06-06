#ifndef LIVE_STREAM_HTTP_HTTP_DEPENDENCIES_H_
#define LIVE_STREAM_HTTP_HTTP_DEPENDENCIES_H_

#include "http.h"

namespace live_stream {

class NetEngine;

struct HttpDependencies {
    NetEngine *net_engine = nullptr;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_HTTP_DEPENDENCIES_H_
