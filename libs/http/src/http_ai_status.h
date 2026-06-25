#ifndef LIVE_STREAM_HTTP_SRC_HTTP_AI_STATUS_H_
#define LIVE_STREAM_HTTP_SRC_HTTP_AI_STATUS_H_

namespace live_stream {

class IAiReader;
class IConfig;

bool IsAiConfigEnabled(IConfig *config);
bool IsAiHealthy(const IAiReader *ai);

}  // namespace live_stream

#endif  // LIVE_STREAM_HTTP_SRC_HTTP_AI_STATUS_H_
