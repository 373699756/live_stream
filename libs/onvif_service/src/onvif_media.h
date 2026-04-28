#ifndef LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_MEDIA_H_
#define LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_MEDIA_H_

#include "infra/status.h"
#include "infra/stream_types.h"
#include "onvif_service.h"

#include <string>

namespace live_stream {
namespace onvif_internal {

struct OnvifBodyResult {
    std::string body;
    bool success = false;
};

infra::Result<infra::StreamId> ParseStreamId(const std::string& body);
std::string ProfilesBody();
std::string ProfileFault(uint32_t* status, std::string* reason);
OnvifBodyResult StreamUriBody(IOnvifUriProvider* uri_provider,
                              infra::StreamId stream_id,
                              uint32_t* status,
                              std::string* reason);
OnvifBodyResult SnapshotUriBody(IOnvifUriProvider* uri_provider,
                                infra::StreamId stream_id,
                                uint32_t* status,
                                std::string* reason);

}  // namespace onvif_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SERVICE_SRC_ONVIF_MEDIA_H_
