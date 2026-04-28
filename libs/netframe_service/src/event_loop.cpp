#include "event_loop.h"

#include "infra/errno_util.h"
#include "infra/time.h"

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <cerrno>
#include <utility>
#include <vector>

namespace live_stream {
namespace netframe_internal {

EventLoop::EventLoop(uint32_t max_events, uint32_t task_capacity)
    : max_events_(max_events == 0 ? 64 : max_events),
      task_capacity_(task_capacity == 0 ? 1 : task_capacity) {}

EventLoop::~EventLoop() { Stop(); }

infra::Status EventLoop::Start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_) {
    return infra::Status::kOk;
  }
  epoll_fd_.Reset(epoll_create1(EPOLL_CLOEXEC));
  timer_fd_.Reset(timerfd_create(CLOCK_MONOTONIC,
                                 TFD_NONBLOCK | TFD_CLOEXEC));
  if (!epoll_fd_.valid() || !timer_fd_.valid()) {
    return infra::Status::kIoError;
  }
  infra::Status status = wakeup_.Open();
  if (status != infra::Status::kOk) {
    return status;
  }
  status = AddRawFdLocked(wakeup_.fd(), EPOLLIN, [this](uint32_t) {
    wakeup_.Drain();
  });
  if (status != infra::Status::kOk) {
    return status;
  }
  status = AddRawFdLocked(timer_fd_.get(), EPOLLIN, [this](uint32_t) {
    DrainTimerFd();
    RunTimers();
  });
  if (status != infra::Status::kOk) {
    return status;
  }
  stopping_ = false;
  running_ = true;
  thread_ = std::thread([this]() { Run(); });
  return infra::Status::kOk;
}

void EventLoop::Stop() {
  bool should_join = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ && !thread_.joinable()) {
      return;
    }
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

infra::Status EventLoop::Post(infra::Task task) {
  if (!task) {
    return infra::Status::kInvalidParam;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_) {
      return infra::Status::kBusy;
    }
    if (tasks_.size() >= task_capacity_) {
      return infra::Status::kBusy;
    }
    tasks_.push_back(std::move(task));
  }
  return wakeup_.Notify();
}

infra::Status EventLoop::AddFd(int fd,
                               uint32_t events,
                               std::function<void(uint32_t)> handler) {
  if (fd < 0 || !handler) {
    return infra::Status::kInvalidParam;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_ || stopping_) {
    return infra::Status::kBusy;
  }
  return AddRawFdLocked(fd, events | EPOLLERR | EPOLLHUP, std::move(handler));
}

infra::Status EventLoop::ModifyFd(int fd, uint32_t events) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_ || stopping_) {
    return infra::Status::kBusy;
  }
  auto it = handlers_.find(fd);
  if (it == handlers_.end()) {
    return infra::Status::kNotFound;
  }
  epoll_event event {};
  event.events = events | EPOLLERR | EPOLLHUP;
  event.data.fd = fd;
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_MOD, fd, &event) != 0) {
    return infra::ErrnoToStatus(errno);
  }
  it->second.events = event.events;
  return infra::Status::kOk;
}

void EventLoop::RemoveFd(int fd) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (epoll_fd_.valid() && fd >= 0) {
    epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr);
  }
  handlers_.erase(fd);
}

infra::Result<NetTimerId> EventLoop::RunAfter(uint32_t delay_ms,
                                              infra::Task task) {
  if (!task) {
    return infra::Result<NetTimerId>::Fail(infra::Status::kInvalidParam);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_ || stopping_) {
    return infra::Result<NetTimerId>::Fail(infra::Status::kBusy);
  }
  const NetTimerId id = next_timer_id_++;
  Timer timer;
  timer.deadline_ms = infra::Time::MonotonicMillis() + delay_ms;
  timer.task = std::move(task);
  timers_[id] = std::move(timer);
  RearmTimerLocked();
  return infra::Result<NetTimerId>::Ok(id);
}

infra::Status EventLoop::CancelTimer(NetTimerId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = timers_.find(id);
  if (it == timers_.end()) {
    return infra::Status::kNotFound;
  }
  timers_.erase(it);
  RearmTimerLocked();
  return infra::Status::kOk;
}

infra::Status EventLoop::AddRawFdLocked(
    int fd, uint32_t events, std::function<void(uint32_t)> handler) {
  epoll_event event {};
  event.events = events;
  event.data.fd = fd;
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &event) != 0) {
    return infra::ErrnoToStatus(errno);
  }
  handlers_[fd] = Handler{events, std::move(handler)};
  return infra::Status::kOk;
}

void EventLoop::CleanupLocked() {
  running_ = false;
  stopping_ = false;
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
  itimerspec spec {};
  if (!timers_.empty()) {
    const int64_t now = infra::Time::MonotonicMillis();
    int64_t next_ms = timers_.begin()->second.deadline_ms;
    for (const auto& entry : timers_) {
      if (entry.second.deadline_ms < next_ms) {
        next_ms = entry.second.deadline_ms;
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
  std::vector<infra::Task> ready;
  const int64_t now = infra::Time::MonotonicMillis();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = timers_.begin(); it != timers_.end();) {
      if (it->second.deadline_ms > now) {
        ++it;
        continue;
      }
      ready.push_back(std::move(it->second.task));
      it = timers_.erase(it);
    }
    RearmTimerLocked();
  }
  for (infra::Task& task : ready) {
    if (task) {
      task();
    }
  }
}

void EventLoop::RunTasks() {
  std::deque<infra::Task> tasks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks.swap(tasks_);
  }
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
  return it == handlers_.end() ? nullptr : it->second.callback;
}

void EventLoop::Run() {
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

}  // namespace netframe_internal
}  // namespace live_stream
