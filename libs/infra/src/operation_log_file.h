/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: operation_log_file.h
 * Brief: Defines internal operation audit storage interfaces.
 */

#ifndef LIVE_STREAM_INFRA_OPERATION_LOG_FILE_H_
#define LIVE_STREAM_INFRA_OPERATION_LOG_FILE_H_

#include "logger.h"

#include <mutex>
#include <string>
#include <vector>

namespace live_stream {

class OperationLogFile {
public:
    explicit OperationLogFile(const LoggerConfig& config);

    bool Open();
    void Close();
    bool Append(const OperationRecord& record);
    std::vector<OperationRecord> Query(
        const OperationLogQuery& query);
    bool Export(const OperationLogExportOptions& options);

private:
    std::vector<std::string> LogPathsNewestFirst() const;
    bool RotateIfNeededLocked();

    LoggerConfig config_;
    bool opened_ = false;
    mutable std::mutex mutex_;
};

}  // namespace live_stream

#endif  // LIVE_STREAM_INFRA_OPERATION_LOG_FILE_H_
