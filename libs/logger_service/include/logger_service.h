/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: logger_service.h
 * Brief: Defines the user operation audit service public interface.
 */

#ifndef LIVE_STREAM_LOGGER_SERVICE_H_
#define LIVE_STREAM_LOGGER_SERVICE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace live_stream {

enum class OperationAction {
    kLogin,
    kLogout,
    kAuthFailed,
    kTokenExpired,
    kModifyConfig,
    kReboot,
    kFactoryReset,
    kUpgrade,
    kTimeSync,
    kNetworkChange,
    kUserManage,
    kPermissionDenied,
};

enum class OperationResult {
    kSuccess,
    kFailed,
    kRejected,
};

struct OperationRecord {
    int64_t timestamp_ms = 0;
    std::string request_id;
    std::string user_name;
    std::string session_id;
    std::string client_ip;
    std::string module;
    OperationAction action = OperationAction::kLogin;
    std::string target;
    OperationResult result = OperationResult::kSuccess;
    std::string reason;
};

struct OperationLogQuery {
    int64_t begin_timestamp_ms = 0;
    int64_t end_timestamp_ms = 0;
    std::string user_name;
    OperationAction action = OperationAction::kLogin;
    OperationResult result = OperationResult::kSuccess;
    bool has_action = false;
    bool has_result = false;
    uint32_t limit = 100;
};

struct OperationLogExportOptions {
    std::string output_path;
    OperationLogQuery query;
};

struct LoggerServiceConfig {
    std::string operation_log_path = "/mnt/flash/logs/operation.log";
    uint32_t queue_capacity = 128;
    uint64_t max_file_size_bytes = 1024U * 1024U;
    uint32_t max_rotate_files = 4;
};

class ILoggerService {
 public:
    virtual ~ILoggerService() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool RecordOperation(const OperationRecord& record) = 0;
    virtual std::vector<OperationRecord> QueryOperations(
        const OperationLogQuery& query) = 0;
    virtual bool ExportOperations(
        const OperationLogExportOptions& options) = 0;
};

std::unique_ptr<ILoggerService> CreateLoggerService(
    const LoggerServiceConfig& config);

const char* OperationActionToString(OperationAction action);
const char* OperationResultToString(OperationResult result);

}  // namespace live_stream

#endif  // LIVE_STREAM_LOGGER_SERVICE_H_
