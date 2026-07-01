#ifndef LIVE_STREAM_SOCKET_IO_SRC_EVENT_LOOP_H_
#define LIVE_STREAM_SOCKET_IO_SRC_EVENT_LOOP_H_

#include "event_fd.h"
#include "fd.h"
#include "socket_io.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace live_stream {
namespace socket_io_internal {

class EventLoop : public event::Loop {
public:
    EventLoop(uint32_t max_events, uint32_t task_capacity,
              int affinity_cpu);
    ~EventLoop();

    EventLoop(const EventLoop &) = delete;
    EventLoop &operator=(const EventLoop &) = delete;

    bool Start(const event::LoopOptions &options = event::LoopOptions()) override;
    void Stop(event::StopMode mode) override;
    event::EventStatus Post(event::Task task) override;
    bool IsCurrentThread() const override;
    bool AddFd(int fd, uint32_t events, std::function<void(uint32_t)> handler);
    bool ModifyFd(int fd, uint32_t events);
    void RemoveFd(int fd);
    event::EventStatus RunAfter(uint32_t delay_ms, event::Task task,
                                event::TimerId *timer_id) override;
    event::EventStatus RunEvery(uint32_t interval_ms, event::Task task,
                                event::TimerId *timer_id) override;
    bool CancelTimer(event::TimerId id) override;
    event::LoopStats GetStats() const override;

private:
    struct Handler {
        uint32_t events = 0;
        std::function<void(uint32_t)> callback;
    };

    struct Timer {
        int64_t deadline_ms = 0;
        uint32_t interval_ms = 0;
        event::Task task;
        bool cancelled = false;
        bool executing = false;
    };

    bool AddRawFdLocked(int fd, uint32_t events,
                        std::function<void(uint32_t)> handler);
    void CleanupLocked();
    void DrainTimerFd();
    // Sets timer_fd_ to wake at the next pending timer deadline, or disarms it
    // when no timers remain. Caller must hold mutex_.
    void SetTimerFdNextWakeupLocked();
    void RunTimers();
    void RunTasks();
    bool ShouldStop() const;
    std::function<void(uint32_t)> FindHandler(int fd);
    void Run();

    const uint32_t max_events_;
    const uint32_t task_capacity_;
    const int affinity_cpu_;
    mutable std::mutex mutex_;
    UniqueFd epoll_fd_;
    UniqueFd timer_fd_;
    EventFd wakeup_;
    std::thread thread_;
    std::thread::id thread_id_;
    std::deque<event::Task> tasks_;
    std::unordered_map<int, Handler> handlers_;
    std::map<event::TimerId, std::shared_ptr<Timer>> timers_;
    event::LoopStats stats_;
    event::TimerId next_timer_id_ = 1;
    bool running_ = false;
    bool stopping_ = false;
};

}  // namespace socket_io_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_SOCKET_IO_SRC_EVENT_LOOP_H_
