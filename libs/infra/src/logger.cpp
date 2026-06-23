#include "logger.h"

#include "operation_log.h"

#include <memory>
#include <mutex>
#include <utility>

namespace live_stream {
namespace {

class LoggerImpl : public ILogger {
public:
    explicit LoggerImpl(std::unique_ptr<IOperationLog> operation_log)
        : operation_log_(std::move(operation_log)) {}

    ~LoggerImpl() override {
        ReleaseInternal();
    }

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!operation_log_) {
            return false;
        }
        if (initialized_) {
            return true;
        }
        if (!operation_log_->Open()) {
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
        StopInternal();
    }

    bool IsStarted() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
    }

    void Release() {
        ReleaseInternal();
    }

private:
    void StopInternal() {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
    }

    void ReleaseInternal() {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
        if (initialized_ && operation_log_) {
            operation_log_->Close();
        }
        initialized_ = false;
    }

public:
    bool RecordOperation(const OperationRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_ || !operation_log_) {
            return false;
        }
        return operation_log_->Append(record);
    }

    std::vector<OperationRecord> QueryOperations(
        const OperationLogQuery& query) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !operation_log_) {
            return {};
        }
        return operation_log_->Query(query);
    }

    bool ExportOperations(
        const OperationLogExportOptions& options) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !operation_log_) {
            return false;
        }
        return operation_log_->Export(options);
    }

private:
    mutable std::mutex mutex_;
    std::unique_ptr<IOperationLog> operation_log_;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<ILogger> CreateLogger(
    const LoggerConfig& config) {
    return std::unique_ptr<ILogger>(new LoggerImpl(
        std::unique_ptr<IOperationLog>(new FileOperationLog(config))));
}

}  // namespace live_stream
