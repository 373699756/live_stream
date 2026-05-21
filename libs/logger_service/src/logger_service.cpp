#include "logger_service.h"

#include "operation_log_store.h"

#include <memory>
#include <mutex>
#include <utility>

namespace live_stream {
namespace {

class LoggerServiceImpl : public ILoggerService {
public:
    explicit LoggerServiceImpl(std::unique_ptr<IOperationLogStore> store)
        : store_(std::move(store)) {}

    ~LoggerServiceImpl() override {
        Stop();
        Release();
    }

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!store_) {
            return false;
        }
        if (initialized_) {
            return true;
        }
        if (!store_->Open()) {
            return false;
        }
        initialized_ = true;
        return true;
    }

    bool Start() override {
        if (!Prepare()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = true;
        return true;
    }

    void Stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
    }

    bool IsStarted() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
    }

    void Release() {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
        if (initialized_ && store_) {
            store_->Close();
        }
        initialized_ = false;
    }

    bool RecordOperation(const OperationRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_ || !store_) {
            return false;
        }
        return store_->Append(record);
    }

    std::vector<OperationRecord> QueryOperations(
        const OperationLogQuery& query) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !store_) {
            return {};
        }
        return store_->Query(query);
    }

    bool ExportOperations(
        const OperationLogExportOptions& options) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !store_) {
            return false;
        }
        return store_->Export(options);
    }

private:
    mutable std::mutex mutex_;
    std::unique_ptr<IOperationLogStore> store_;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<ILoggerService> CreateLoggerService(
    const LoggerServiceConfig& config) {
    return std::unique_ptr<ILoggerService>(new LoggerServiceImpl(
        std::unique_ptr<IOperationLogStore>(new FileOperationLogStore(config))));
}

}  // namespace live_stream
