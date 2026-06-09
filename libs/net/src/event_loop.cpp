#include "event_loop.h"

#include "infra/time.h"
#include "socket_util.h"

#include <cerrno>
#include <pthread.h>
#include <sched.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <utility>
#include <vector>

namespace live_stream {
namespace net_internal {
namespace {

void SetCurrentThreadAffinity(int cpu) {
    if (cpu < 0 || cpu >= CPU_SETSIZE) {
        return;
    }
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(static_cast<unsigned>(cpu), &cpu_set);
    (void)pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
}

}  // namespace

EventLoop::EventLoop(uint32_t max_events, uint32_t task_capacity,
                     int affinity_cpu)
    : max_events_(max_events == 0 ? 64 : max_events),
      task_capacity_(task_capacity == 0 ? 1 : task_capacity),
      affinity_cpu_(affinity_cpu) {}

EventLoop::~EventLoop() { Stop(); }

bool EventLoop::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return true;
    }
    // 每个 EventLoop 只由自己的 IO 线程执行 fd/timer 回调；跨线程工作统一先进入
    // tasks_，再用 eventfd 唤醒 epoll，避免协议模块直接抢占 IO 线程状态。
    const int epoll_fd = CreateEpollFd(EPOLL_CLOEXEC);
    const int timer_fd =
        CreateTimerFd(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    epoll_fd_.Reset(epoll_fd);
    timer_fd_.Reset(timer_fd);
    if (!epoll_fd_.valid() || !timer_fd_.valid()) {
        return false;
    }
    if (!wakeup_.Open()) {
        return false;
    }
    if (!AddRawFdLocked(wakeup_.fd(), EPOLLIN,
                        [this](uint32_t) { wakeup_.Drain(); })) {
        return false;
    }
    if (!AddRawFdLocked(timer_fd_.get(), EPOLLIN, [this](uint32_t) {
            DrainTimerFd();
            RunTimers();
        })) {
        return false;
    }
    stopping_ = false;
    running_ = true;
    thread_ = std::thread([this]() { Run(); });
    return true;
}

void EventLoop::Stop() {
    bool should_join = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && !thread_.joinable()) {
            return;
        }
        // Stop() 可能从 IO 线程自己的回调里触发，此时不能 join 自己；
        // 只设置 stopping_ 并通过 wakeup_ 让 Run() 尽快退出。
        stopping_ = true;
        should_join =
            thread_.joinable() && thread_.get_id() != std::this_thread::get_id();
    }
    (void)wakeup_.Notify();
    if (should_join) {
        thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    CleanupLocked();
}

bool EventLoop::Post(infra::Task task) {
    if (!task) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || stopping_) {
            return false;
        }
        // task_capacity_ 是 IO 线程的背压边界；队列满时上层必须感知失败，
        // 不能在网络层无限堆积控制任务或媒体发送任务。
        if (tasks_.size() >= task_capacity_) {
            return false;
        }
        tasks_.push_back(std::move(task));
    }
    return wakeup_.Notify();
}

bool EventLoop::IsCurrentThread() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return thread_id_ == std::this_thread::get_id();
}

bool EventLoop::AddFd(int fd, uint32_t events,
                      std::function<void(uint32_t)> handler) {
    if (fd < 0 || !handler) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_) {
        return false;
    }
    return AddRawFdLocked(fd, events | EPOLLERR | EPOLLHUP, std::move(handler));
}

bool EventLoop::ModifyFd(int fd, uint32_t events) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_) {
        return false;
    }
    auto it = handlers_.find(fd);
    if (it == handlers_.end()) {
        return false;
    }
    // 调用方只传业务关心的事件；EPOLLERR/EPOLLHUP 始终保留，
    // 让 TcpSession 能统一走 close path。
    epoll_event event{};
    event.events = events | EPOLLERR | EPOLLHUP;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_MOD, fd, &event) != 0) {
        return false;
    }
    it->second.events = event.events;
    return true;
}

void EventLoop::RemoveFd(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (epoll_fd_.valid() && fd >= 0) {
        epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr);
    }
    // fd 从 handlers_ 删除后，即使 epoll 已经返回旧事件，FindHandler() 也会拿不到
    // 回调，从而避免关闭后的 fd 被再次处理。
    handlers_.erase(fd);
}

NetTimerId EventLoop::RunAfter(NetTimerId id, uint32_t delay_ms,
                               infra::Task task) {
    if (id == 0 || !task) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_) {
        return 0;
    }
    if (timers_.find(id) != timers_.end()) {
        return 0;
    }
    std::shared_ptr<Timer> timer(new Timer());
    timer->deadline_ms = infra::Time::MonotonicMillis() + delay_ms;
    timer->interval_ms = 0;
    timer->task = std::move(task);
    timers_[id] = timer;
    RearmTimerLocked();
    return id;
}

NetTimerId EventLoop::RunEvery(NetTimerId id, uint32_t interval_ms,
                               infra::Task task) {
    if (id == 0 || interval_ms == 0 || !task) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_) {
        return 0;
    }
    if (timers_.find(id) != timers_.end()) {
        return 0;
    }
    std::shared_ptr<Timer> timer(new Timer());
    timer->deadline_ms = infra::Time::MonotonicMillis() + interval_ms;
    timer->interval_ms = interval_ms;
    timer->task = std::move(task);
    timers_[id] = timer;
    RearmTimerLocked();
    return id;
}

bool EventLoop::CancelTimer(NetTimerId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = timers_.find(id);
    if (it == timers_.end()) {
        return false;
    }
    // timer 可能正在 IO 线程回调中执行。执行中的 timer 只打 cancelled 标记，
    // 回调返回后由 RunTimers() 收尾删除，保证 CancelTimer() 后不会再次触发。
    it->second->cancelled = true;
    if (!it->second->executing) {
        timers_.erase(it);
    }
    RearmTimerLocked();
    return true;
}

bool EventLoop::AddRawFdLocked(int fd, uint32_t events,
                               std::function<void(uint32_t)> handler) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &event) != 0) {
        return false;
    }
    handlers_[fd] = Handler{events, std::move(handler)};
    return true;
}

void EventLoop::CleanupLocked() {
    running_ = false;
    stopping_ = false;
    thread_id_ = std::thread::id();
    tasks_.clear();
    handlers_.clear();
    timers_.clear();
    wakeup_.Close();
    timer_fd_.Reset();
    epoll_fd_.Reset();
}

void EventLoop::DrainTimerFd() {
    uint64_t expirations = 0;
    while (read(timer_fd_.get(), &expirations, sizeof(expirations)) > 0) {
    }
}

void EventLoop::RearmTimerLocked() {
    if (!timer_fd_.valid()) {
        return;
    }
    itimerspec spec{};
    if (!timers_.empty()) {
        // timers_ 按 id 存储，不按 deadline 排序，因此重设 timerfd 前必须扫描
        // 最近 deadline。timer 数量由协议连接和 session 数限制，线性扫描足够直接。
        const int64_t now = infra::Time::MonotonicMillis();
        int64_t next_ms = timers_.begin()->second->deadline_ms;
        for (const auto &entry : timers_) {
            if (entry.second->deadline_ms < next_ms) {
                next_ms = entry.second->deadline_ms;
            }
        }
        int64_t delay_ms = next_ms - now;
        if (delay_ms < 1) {
            delay_ms = 1;
        }
        spec.it_value.tv_sec = delay_ms / 1000;
        spec.it_value.tv_nsec = (delay_ms % 1000) * 1000000LL;
    }
    (void)timerfd_settime(timer_fd_.get(), 0, &spec, nullptr);
}

void EventLoop::RunTimers() {
    std::vector<std::pair<NetTimerId, std::shared_ptr<Timer>>> ready;
    const int64_t now = infra::Time::MonotonicMillis();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &entry : timers_) {
            if (entry.second->deadline_ms <= now) {
                ready.push_back(entry);
            }
        }
    }
    // 先拷贝 ready timer，再逐个执行。执行前重新校验 id 和 shared_ptr，
    // 可以挡住回调期间的取消、重建或 Stop() 清理。
    for (const auto &entry : ready) {
        const NetTimerId id = entry.first;
        const std::shared_ptr<Timer> timer = entry.second;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = timers_.find(id);
            if (it == timers_.end() || it->second != timer ||
                timer->deadline_ms > now || timer->cancelled) {
                continue;
            }
            timer->executing = true;
        }
        if (!timer->task) {
            std::lock_guard<std::mutex> lock(mutex_);
            timer->executing = false;
            continue;
        }
        timer->task();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            timer->executing = false;
            auto it = timers_.find(id);
            if (it == timers_.end() || it->second != timer ||
                timer->cancelled) {
                if (it != timers_.end() && it->second == timer &&
                    timer->cancelled) {
                    timers_.erase(it);
                }
                continue;
            }
            if (timer->interval_ms != 0) {
                timer->deadline_ms =
                    infra::Time::MonotonicMillis() + timer->interval_ms;
            } else {
                timers_.erase(it);
            }
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    RearmTimerLocked();
}

void EventLoop::RunTasks() {
    std::deque<infra::Task> tasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks.swap(tasks_);
    }
    // 任务在锁外执行，避免协议回调里再次 Post()/CancelTimer() 时自锁。
    while (!tasks.empty()) {
        infra::Task task = std::move(tasks.front());
        tasks.pop_front();
        if (task) {
            task();
        }
    }
}

bool EventLoop::ShouldStop() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopping_;
}

std::function<void(uint32_t)> EventLoop::FindHandler(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = handlers_.find(fd);
    // 返回 std::function 副本，在锁外执行实际 handler。handler 可以安全调用
    // RemoveFd()/ModifyFd()/Post()，不会和 EventLoop mutex 自锁。
    return it == handlers_.end() ? nullptr : it->second.callback;
}

void EventLoop::Run() {
    SetCurrentThreadAffinity(affinity_cpu_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        thread_id_ = std::this_thread::get_id();
    }
    std::vector<epoll_event> events(max_events_);
    while (!ShouldStop()) {
        const int count = epoll_wait(epoll_fd_.get(), events.data(),
                                     static_cast<int>(events.size()), -1);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int i = 0; i < count; ++i) {
            const int fd = events[static_cast<size_t>(i)].data.fd;
            auto handler = FindHandler(fd);
            if (handler) {
                handler(events[static_cast<size_t>(i)].events);
            }
        }
        RunTasks();
    }
    RunTasks();
}

}  // namespace net_internal
}  // namespace live_stream
