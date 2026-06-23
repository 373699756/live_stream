#ifndef LIVE_STREAM_EVENT_LOOP_H_
#define LIVE_STREAM_EVENT_LOOP_H_

#include "executor.h"

#include <cstdint>
#include <memory>
#include <string>

namespace live_stream {
namespace event {

using TimerId = uint64_t;

struct LoopOptions {
    std::string name;
    uint32_t queue_capacity = 4096;
    bool enable_thread_affinity = false;
    uint32_t cpu = 0;
};

struct LoopStats {
    uint64_t posted = 0;
    uint64_t completed = 0;
    uint64_t rejected = 0;
    uint64_t timer_fired = 0;
    uint64_t timer_cancelled = 0;
    uint32_t pending = 0;
    uint32_t max_pending = 0;
    uint32_t active_timers = 0;
    bool running = false;
};

class Loop {
public:
    Loop();
    virtual ~Loop();

    Loop(const Loop &) = delete;
    Loop &operator=(const Loop &) = delete;

    virtual bool Start(const LoopOptions &options = LoopOptions());
    virtual void Stop(StopMode mode);
    virtual EventStatus Post(Task task);
    virtual EventStatus RunAfter(uint32_t delay_ms, Task task,
                                 TimerId *timer_id);
    virtual EventStatus RunEvery(uint32_t interval_ms, Task task,
                                 TimerId *timer_id);
    virtual bool CancelTimer(TimerId timer_id);
    virtual bool IsCurrentThread() const;
    virtual LoopStats GetStats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace event
}  // namespace live_stream

#endif  // LIVE_STREAM_EVENT_LOOP_H_
