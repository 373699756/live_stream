/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: operation_log.h
 * Brief: Defines internal operation audit storage interfaces.
 */

#ifndef LIVE_STREAM_LOGGER_OPERATION_LOG_H_
#define LIVE_STREAM_LOGGER_OPERATION_LOG_H_

#include "logger.h"

#include <mutex>
#include <string>

namespace live_stream {

class IOperationLog {
public:
    virtual ~IOperationLog() = default;

    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual bool Append(const OperationRecord& record) = 0;
    virtual std::vector<OperationRecord> Query(
        const OperationLogQuery& query) = 0;
    virtual bool Export(const OperationLogExportOptions& options) = 0;
};

class FileOperationLog : public IOperationLog {
public:
    explicit FileOperationLog(const LoggerConfig& config);

    bool Open() override;
    void Close() override;
    bool Append(const OperationRecord& record) override;
    std::vector<OperationRecord> Query(
        const OperationLogQuery& query) override;
    bool Export(const OperationLogExportOptions& options) override;

private:
    std::vector<std::string> LogPathsNewestFirst() const;
    bool RotateIfNeededLocked();

    LoggerConfig config_;
    bool opened_ = false;
    mutable std::mutex mutex_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_LOGGER_OPERATION_LOG_H_
