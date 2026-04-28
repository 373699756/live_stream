#include "infra/executor.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace infra {
namespace {

uint32_t AutoWorkerCount() {
    const uint32_t hardware = std::thread::hardware_concurrency();
    if (hardware == 0) {
        return 2;
    }
    return std::max<uint32_t>(1, std::min<uint32_t>(hardware, 4));
}

}  // namespace

class Executor::Impl {
 public:
    Status Start(const ExecutorOptions& options) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return Status::kOk;
        }

        options_ = options;
        if (options_.worker_count == 0) {
            options_.worker_count = AutoWorkerCount();
        }
        if (options_.queue_capacity == 0 || options_.worker_count == 0) {
            return Status::kInvalidParam;
        }

        stopping_ = false;
        discard_ = false;
        running_ = true;
        head_ = 0;
        tail_ = 0;
        size_ = 0;
        tasks_.clear();
        tasks_.resize(options_.queue_capacity);
        workers_.reserve(options_.worker_count);
        for (uint32_t i = 0; i < options_.worker_count; ++i) {
            workers_.push_back(std::thread(&Impl::WorkerLoop, this));
        }
        return Status::kOk;
    }

    void Stop(StopMode mode) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ && workers_.empty()) {
                return;
            }
            stopping_ = true;
            running_ = false;
            discard_ = mode == StopMode::kDiscard;
            if (discard_) {
                ClearQueueLocked();
            }
        }
        condition_.notify_all();

        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();

        std::lock_guard<std::mutex> lock(mutex_);
        ClearQueueLocked();
        running_count_ = 0;
    }

    Status Post(Task task) {
        if (!task) {
            return Status::kInvalidParam;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ || stopping_) {
                ++stats_.rejected;
                return Status::kBusy;
            }
            if (size_ >= options_.queue_capacity) {
                ++stats_.rejected;
                return Status::kBusy;
            }
            tasks_[tail_] = std::move(task);
            tail_ = (tail_ + 1) % options_.queue_capacity;
            ++size_;
            ++stats_.posted;
            stats_.max_pending = std::max<uint32_t>(
                stats_.max_pending, static_cast<uint32_t>(size_));
        }
        condition_.notify_one();
        return Status::kOk;
    }

    ExecutorStats GetStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        ExecutorStats stats = stats_;
        stats.pending = static_cast<uint32_t>(size_);
        stats.running = running_count_;
        stats.worker_count = options_.worker_count;
        return stats;
    }

 private:
    void WorkerLoop() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]() {
                    return stopping_ || size_ > 0;
                });
                if ((stopping_ && (discard_ || size_ == 0)) ||
                    (!running_ && size_ == 0)) {
                    break;
                }
                task = std::move(tasks_[head_]);
                tasks_[head_].Reset();
                head_ = (head_ + 1) % options_.queue_capacity;
                --size_;
                ++running_count_;
                ++stats_.wakeups;
            }

            task();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                --running_count_;
                ++stats_.completed;
                if (stopping_ && size_ == 0) {
                    condition_.notify_all();
                }
            }
        }
    }

    void ClearQueueLocked() {
        for (uint32_t i = 0; i < size_; ++i) {
            const uint32_t index = (head_ + i) % options_.queue_capacity;
            tasks_[index].Reset();
        }
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

    ExecutorOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<Task> tasks_;
    std::vector<std::thread> workers_;
    ExecutorStats stats_;
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
    uint32_t size_ = 0;
    uint32_t running_count_ = 0;
    bool running_ = false;
    bool stopping_ = false;
    bool discard_ = false;
};

Executor::Executor() : impl_(new Impl()) {}
Executor::~Executor() { Stop(StopMode::kDiscard); }
Status Executor::Start(const ExecutorOptions& options) {
    return impl_->Start(options);
}
void Executor::Stop(StopMode mode) {
    impl_->Stop(mode);
}
Status Executor::Post(Task task) {
    return impl_->Post(std::move(task));
}
ExecutorStats Executor::GetStats() const {
    return impl_->GetStats();
}

}  // namespace infra
