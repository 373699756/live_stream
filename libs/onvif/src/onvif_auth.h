#ifndef LIVE_STREAM_ONVIF_SRC_ONVIF_AUTH_H_
#define LIVE_STREAM_ONVIF_SRC_ONVIF_AUTH_H_

#include "auth.h"
#include "onvif_types.h"

#include <string>

namespace live_stream {
namespace onvif {

bool AuthorizeOnvifAction(IAuth *auth,
                          bool enable_auth,
                          const std::string &headers,
                          OnvifAction action);

}  // namespace onvif
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SRC_ONVIF_AUTH_H_
