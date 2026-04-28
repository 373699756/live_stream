/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: operation_log_store.h
 * Brief: Defines internal operation audit storage interfaces.
 */

#ifndef LIVE_STREAM_LOGGER_SERVICE_OPERATION_LOG_STORE_H_
#define LIVE_STREAM_LOGGER_SERVICE_OPERATION_LOG_STORE_H_

#include "logger_service.h"

#include <mutex>
#include <string>

namespace live_stream {

class IOperationLogStore {
 public:
    virtual ~IOperationLogStore() = default;

    virtual infra::Status Open() = 0;
    virtual void Close() = 0;
    virtual infra::Status Append(const OperationRecord& record) = 0;
    virtual infra::Result<std::vector<OperationRecord>> Query(
        const OperationLogQuery& query) = 0;
    virtual infra::Status Export(const OperationLogExportOptions& options) = 0;
};

class FileOperationLogStore : public IOperationLogStore {
 public:
    explicit FileOperationLogStore(const LoggerServiceConfig& config);

    infra::Status Open() override;
    void Close() override;
    infra::Status Append(const OperationRecord& record) override;
    infra::Result<std::vector<OperationRecord>> Query(
        const OperationLogQuery& query) override;
    infra::Status Export(const OperationLogExportOptions& options) override;

 private:
    std::vector<std::string> LogPathsNewestFirst() const;
    infra::Status RotateIfNeededLocked();

    LoggerServiceConfig config_;
    bool opened_ = false;
    mutable std::mutex mutex_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_LOGGER_SERVICE_OPERATION_LOG_STORE_H_
