#ifndef LIVE_STREAM_RTSP_SRC_RTSP_SESSION_EVENT_H_
#define LIVE_STREAM_RTSP_SRC_RTSP_SESSION_EVENT_H_

#include "event.h"
#include "rtsp_session_table.h"

#include <mutex>
#include <string>

namespace live_stream {

class RtspSessionEvent {
public:
    RtspSessionEvent(event::EventCenter *event_center,
                     std::mutex *mutex,
                     RtspSessionTable *session_table,
                     const char *source);

    void Publish(event::EventType type, const std::string &target);

private:
    event::EventCenter *event_center_ = nullptr;
    std::mutex *mutex_ = nullptr;
    RtspSessionTable *session_table_ = nullptr;
    const char *source_ = nullptr;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_RTSP_SRC_RTSP_SESSION_EVENT_H_
