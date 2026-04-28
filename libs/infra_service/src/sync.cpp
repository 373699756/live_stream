#include "infra/sync.h"

#include <chrono>

namespace infra {

Status ConditionVariable::Wait(Mutex* mutex) {
    if (mutex == nullptr) {
        return Status::kInvalidParam;
    }

    std::unique_lock<std::mutex> lock(mutex->mutex_, std::adopt_lock);
    condition_variable_.wait(lock);
    lock.release();
    return Status::kOk;
}

Status ConditionVariable::WaitFor(Mutex* mutex, uint32_t timeout_ms) {
    if (mutex == nullptr) {
        return Status::kInvalidParam;
    }

    std::unique_lock<std::mutex> lock(mutex->mutex_, std::adopt_lock);
    const std::cv_status status = condition_variable_.wait_for(
        lock, std::chrono::milliseconds(timeout_ms));
    lock.release();
    return status == std::cv_status::timeout ? Status::kTimeout : Status::kOk;
}

void ConditionVariable::NotifyOne() {
    condition_variable_.notify_one();
}

void ConditionVariable::NotifyAll() {
    condition_variable_.notify_all();
}

}  // namespace infra
