#ifndef LIVE_STREAM_NETFRAME_SERVICE_SRC_EVENT_LOOP_H_
#define LIVE_STREAM_NETFRAME_SERVICE_SRC_EVENT_LOOP_H_

#include "infra/event_fd.h"
#include "infra/fd.h"
#include "netframe_service.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace live_stream {
namespace netframe_internal {

class EventLoop {
 public:
  EventLoop(uint32_t max_events, uint32_t task_capacity);
  ~EventLoop();

  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;

  infra::Status Start();
  void Stop();
  infra::Status Post(infra::Task task);
  infra::Status AddFd(int fd,
                      uint32_t events,
                      std::function<void(uint32_t)> handler);
  infra::Status ModifyFd(int fd, uint32_t events);
  void RemoveFd(int fd);
  infra::Result<NetTimerId> RunAfter(uint32_t delay_ms, infra::Task task);
  infra::Status CancelTimer(NetTimerId id);

 private:
  struct Handler {
    uint32_t events = 0;
    std::function<void(uint32_t)> callback;
  };

  struct Timer {
    int64_t deadline_ms = 0;
    infra::Task task;
  };

  infra::Status AddRawFdLocked(int fd,
                               uint32_t events,
                               std::function<void(uint32_t)> handler);
  void CleanupLocked();
  void DrainTimerFd();
  void RearmTimerLocked();
  void RunTimers();
  void RunTasks();
  bool ShouldStop() const;
  std::function<void(uint32_t)> FindHandler(int fd);
  void Run();

  const uint32_t max_events_;
  const uint32_t task_capacity_;
  mutable std::mutex mutex_;
  infra::UniqueFd epoll_fd_;
  infra::UniqueFd timer_fd_;
  infra::EventFd wakeup_;
  std::thread thread_;
  std::deque<infra::Task> tasks_;
  std::unordered_map<int, Handler> handlers_;
  std::map<NetTimerId, Timer> timers_;
  NetTimerId next_timer_id_ = 1;
  bool running_ = false;
  bool stopping_ = false;
};

}  // namespace netframe_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_NETFRAME_SERVICE_SRC_EVENT_LOOP_H_
