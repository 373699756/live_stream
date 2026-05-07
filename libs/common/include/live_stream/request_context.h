#ifndef LIVE_STREAM_COMMON_REQUEST_CONTEXT_H_
#define LIVE_STREAM_COMMON_REQUEST_CONTEXT_H_

#include <string>

namespace live_stream {

struct RequestContext {
  std::string request_id;
  std::string user_name;
  std::string session_id;
  std::string client_ip;
  std::string user_agent;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_COMMON_REQUEST_CONTEXT_H_
