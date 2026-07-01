#ifndef LIVE_STREAM_HTTP_SRC_HANDLERS_MEDIA_PREVIEW_RESPONSE_H_
#define LIVE_STREAM_HTTP_SRC_HANDLERS_MEDIA_PREVIEW_RESPONSE_H_

#include "http.h"
#include "json.h"
#include "media/stream_types.h"

namespace live_stream {

class IConfig;
class IRtspSessionReader;

Json BuildMediaPreviewResponse(IConfig *config,
                               IRtspSessionReader *rtsp_reader,
                               const HttpRequest &request,
                               StreamId stream_id);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HANDLERS_MEDIA_PREVIEW_RESPONSE_H_
