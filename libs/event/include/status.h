#ifndef LIVE_STREAM_EVENT_STATUS_H_
#define LIVE_STREAM_EVENT_STATUS_H_

namespace live_stream {
namespace event {

enum class EventStatus {
    kOk = 0,
    kNotStarted,
    kInvalid,
    kQueueFull,
    kNotFound,
    kCancelled,
};

enum class StopMode {
    kDrain,
    kDiscard,
};

}  // namespace event
}  // namespace live_stream

#endif  // LIVE_STREAM_EVENT_STATUS_H_
