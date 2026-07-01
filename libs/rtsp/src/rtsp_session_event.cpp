#include "rtsp_session_event.h"

#include <cstdint>

namespace live_stream {

RtspSessionEvent::RtspSessionEvent(event::EventCenter *event_center,
                                   std::mutex *mutex,
                                   RtspSessionTable *session_table,
                                   const char *source)
    : event_center_(event_center),
      mutex_(mutex),
      session_table_(session_table),
      source_(source) {}

void RtspSessionEvent::Publish(event::EventType type,
                               const std::string &target) {
    if (event_center_ == nullptr || mutex_ == nullptr ||
        session_table_ == nullptr) {
        return;
    }
    size_t active_sessions = 0;
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        active_sessions = session_table_->Size();
    }
    event::Event rtsp_event;
    rtsp_event.type = type;
    rtsp_event.source = source_ == nullptr ? "" : source_;
    rtsp_event.target = target;
    rtsp_event.value = static_cast<int32_t>(active_sessions);
    (void)event_center_->Publish(rtsp_event);
}

}  // namespace live_stream
