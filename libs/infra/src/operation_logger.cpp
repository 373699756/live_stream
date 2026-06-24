#include "logger.h"

#include "operation_log_file.h"

#include <memory>
#include <mutex>
#include <utility>

namespace live_stream {
namespace {

class OperationLogger : public ILogger {
public:
    explicit OperationLogger(std::unique_ptr<OperationLogFile> operation_log_file)
        : operation_log_file_(std::move(operation_log_file)) {}

    ~OperationLogger() override {
        ReleaseInternal();
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

    bool RecordOperation(const OperationRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_ || !operation_log_file_) {
            return false;
        }
        return operation_log_file_->Append(record);
    }

    std::vector<OperationRecord> QueryOperations(
        const OperationLogQuery& query) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !operation_log_file_) {
            return {};
        }
        return operation_log_file_->Query(query);
    }

    bool ExportOperations(
        const OperationLogExportOptions& options) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !operation_log_file_) {
            return false;
        }
        return operation_log_file_->Export(options);
    }

private:
    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!operation_log_file_) {
            return false;
        }
        if (initialized_) {
            return true;
        }
        if (!operation_log_file_->Open()) {
            return false;
        }
        initialized_ = true;
        return true;
    }

    void StopInternal() {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
    }

    void ReleaseInternal() {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
        if (initialized_ && operation_log_file_) {
            operation_log_file_->Close();
        }
        initialized_ = false;
    }

    mutable std::mutex mutex_;
    std::unique_ptr<OperationLogFile> operation_log_file_;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<ILogger> CreateLogger(
    const LoggerConfig& config) {
    return std::unique_ptr<ILogger>(new OperationLogger(
        std::unique_ptr<OperationLogFile>(new OperationLogFile(config))));
}

}  // namespace live_stream
