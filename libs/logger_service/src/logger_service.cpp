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
        Deinit();
    }

    infra::Status Init() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!store_) {
            return infra::Status::kInvalidParam;
        }
        if (initialized_) {
            return infra::Status::kOk;
        }
        const infra::Status error = store_->Open();
        if (error != infra::Status::kOk) {
            return error;
        }
        initialized_ = true;
        return infra::Status::kOk;
    }

    infra::Status Start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !store_) {
            return infra::Status::kInternalError;
        }
        started_ = true;
        return infra::Status::kOk;
    }

    void Stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
    }

    void Deinit() override {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
        if (initialized_ && store_) {
            store_->Close();
        }
        initialized_ = false;
    }

    const char* Name() const override {
        return "logger_service";
    }

    infra::Status RecordOperation(const OperationRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_ || !store_) {
            return infra::Status::kInternalError;
        }
        return store_->Append(record);
    }

    infra::Result<std::vector<OperationRecord>> QueryOperations(
        const OperationLogQuery& query) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !store_) {
            return infra::Result<std::vector<OperationRecord>>::Fail(
                infra::Status::kInternalError);
        }
        return store_->Query(query);
    }

    infra::Status ExportOperations(
        const OperationLogExportOptions& options) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !store_) {
            return infra::Status::kInternalError;
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
