#ifndef LIVE_STREAM_ONVIF_SRC_ONVIF_MEDIA_SERVICE_H_
#define LIVE_STREAM_ONVIF_SRC_ONVIF_MEDIA_SERVICE_H_

#include "media/stream_types.h"
#include "onvif_server.h"
#include "onvif_types.h"

#include <string>

namespace live_stream {
namespace onvif {

struct OnvifMediaUris {
    std::string stream_main;
    std::string stream_sub;
    std::string snapshot_main;
    std::string snapshot_sub;
};

OnvifMediaUris BuildOnvifMediaUris(const OnvifServerOptions &options,
                                   const OnvifServerDependencies &dependencies,
                                   const std::string &advertise_ip);
bool ParseProfileToken(const std::string &body, StreamId *stream_id);
std::string BuildProfilesBody(const OnvifMediaUris &media_uris);
std::string BuildProfileFaultBody(uint32_t *status, std::string *reason);
OnvifBody BuildStreamUriBody(const OnvifMediaUris &media_uris,
                             StreamId stream_id,
                             uint32_t *status,
                             std::string *reason);
OnvifBody BuildSnapshotUriBody(const OnvifMediaUris &media_uris,
                               StreamId stream_id,
                               uint32_t *status,
                               std::string *reason);

}  // namespace onvif
}  // namespace live_stream

#endif  // LIVE_STREAM_ONVIF_SRC_ONVIF_MEDIA_SERVICE_H_
