#ifndef LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_AUTH_H_
#define LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_AUTH_H_

#include "auth_service.h"
#include "infra/status.h"
#include "onvif_types.h"

#include <string>

namespace live_stream {
namespace onvif_internal {

infra::Status AuthorizeOnvifRequest(IAuthService* auth_service,
                                    bool enable_auth,
                                    const std::string& headers,
                                    OnvifAction action);

}  // namespace onvif_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_AUTH_H_
