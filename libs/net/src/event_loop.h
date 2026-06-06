#ifndef LIVE_STREAM_NET_SRC_EVENT_LOOP_H_
#define LIVE_STREAM_NET_SRC_EVENT_LOOP_H_

#include "event_fd.h"
#include "fd.h"
#include "net.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace live_stream {
namespace net_internal {

class EventLoop {
public:
    EventLoop(uint32_t max_events, uint32_t task_capacity);
    ~EventLoop();

    EventLoop(const EventLoop &) = delete;
    EventLoop &operator=(const EventLoop &) = delete;

    bool Start();
    void Stop();
    bool Post(infra::Task task);
    bool AddFd(int fd, uint32_t events, std::function<void(uint32_t)> handler);
    bool ModifyFd(int fd, uint32_t events);
    void RemoveFd(int fd);
    NetTimerId RunAfter(NetTimerId id, uint32_t delay_ms, infra::Task task);
    NetTimerId RunEvery(NetTimerId id, uint32_t interval_ms,
                        infra::Task task);
    bool CancelTimer(NetTimerId id);

private:
    struct Handler {
        uint32_t events = 0;
        std::function<void(uint32_t)> callback;
    };

    struct Timer {
        int64_t deadline_ms = 0;
        uint32_t interval_ms = 0;
        infra::Task task;
        bool cancelled = false;
        bool executing = false;
    };

    bool AddRawFdLocked(int fd, uint32_t events,
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
    UniqueFd epoll_fd_;
    UniqueFd timer_fd_;
    EventFd wakeup_;
    std::thread thread_;
    std::deque<infra::Task> tasks_;
    std::unordered_map<int, Handler> handlers_;
    std::map<NetTimerId, std::shared_ptr<Timer>> timers_;
    bool running_ = false;
    bool stopping_ = false;
};

}  // namespace net_internal
}  // namespace live_stream

#endif  // LIVE_STREAM_NET_SRC_EVENT_LOOP_H_
