/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: file_operation_log_store.cpp
 * Brief: Implements the JSON Lines file backend for operation audit records.
 */

#include "operation_log_store.h"

#include "infra/fs.h"
#include "operation_record_codec.h"

#include <algorithm>

namespace live_stream {
namespace {

bool MatchesQuery(const OperationRecord& record, const OperationLogQuery& query) {
    if (query.begin_timestamp_ms > 0 &&
        record.timestamp_ms < query.begin_timestamp_ms) {
        return false;
    }
    if (query.end_timestamp_ms > 0 &&
        record.timestamp_ms > query.end_timestamp_ms) {
        return false;
    }
    if (!query.user_name.empty() && record.user_name != query.user_name) {
        return false;
    }
    if (query.has_action && record.action != query.action) {
        return false;
    }
    if (query.has_result && record.result != query.result) {
        return false;
    }
    return true;
}

std::vector<std::string> SplitLines(const std::string& content) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find('\n', start);
        if (end == std::string::npos) {
            end = content.size();
        }
        if (end > start) {
            lines.push_back(content.substr(start, end - start));
        }
        start = end + 1;
    }
    return lines;
}

}  // namespace

FileOperationLogStore::FileOperationLogStore(const LoggerServiceConfig& config)
    : config_(config) {}

infra::Status FileOperationLogStore::Open() {
    if (config_.operation_log_path.empty()) {
        return infra::Status::kInvalidParam;
    }
    if (config_.max_file_size_bytes == 0) {
        return infra::Status::kInvalidParam;
    }

    const infra::Status mkdir_result =
        infra::Path::MakeDirs(infra::Path::DirName(config_.operation_log_path));
    if (mkdir_result != infra::Status::kOk) {
        return mkdir_result;
    }

    const infra::Status touch_result =
        infra::File::Append(config_.operation_log_path, "");
    if (touch_result != infra::Status::kOk) {
        return touch_result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    opened_ = true;
    return infra::Status::kOk;
}

void FileOperationLogStore::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    opened_ = false;
}

infra::Status FileOperationLogStore::Append(const OperationRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!opened_) {
        return infra::Status::kInternalError;
    }

    const infra::Status rotate_result = RotateIfNeededLocked();
    if (rotate_result != infra::Status::kOk) {
        return rotate_result;
    }

    return infra::File::Append(config_.operation_log_path,
                               EncodeOperationRecord(record));
}

infra::Result<std::vector<OperationRecord>> FileOperationLogStore::Query(
    const OperationLogQuery& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!opened_) {
        return infra::Result<std::vector<OperationRecord>>::Fail(
            infra::Status::kInternalError);
    }

    std::vector<OperationRecord> records;
    const std::vector<std::string> paths = LogPathsNewestFirst();
    for (const std::string& path : paths) {
        if (!infra::File::Exists(path)) {
            continue;
        }

        infra::Result<std::string> content = infra::File::ReadAll(path);
        if (!content.IsOk()) {
            return infra::Result<std::vector<OperationRecord>>::Fail(
                content.status);
        }

        std::vector<std::string> lines = SplitLines(content.value);
        for (std::vector<std::string>::const_reverse_iterator it = lines.rbegin();
             it != lines.rend(); ++it) {
            OperationRecord record;
            if (!DecodeOperationRecord(*it, &record)) {
                continue;
            }
            if (!MatchesQuery(record, query)) {
                continue;
            }
            records.push_back(record);
            if (query.limit > 0 && records.size() >= query.limit) {
                return infra::Result<std::vector<OperationRecord>>::Ok(records);
            }
        }
    }

    return infra::Result<std::vector<OperationRecord>>::Ok(records);
}

infra::Status FileOperationLogStore::Export(
    const OperationLogExportOptions& options) {
    if (options.output_path.empty()) {
        return infra::Status::kInvalidParam;
    }

    infra::Result<std::vector<OperationRecord>> query_result =
        Query(options.query);
    if (!query_result.IsOk()) {
        return query_result.status;
    }

    const infra::Status mkdir_result =
        infra::Path::MakeDirs(infra::Path::DirName(options.output_path));
    if (mkdir_result != infra::Status::kOk) {
        return mkdir_result;
    }

    std::string output;
    for (const OperationRecord& record : query_result.value) {
        output += EncodeOperationRecord(record);
    }
    return infra::File::WriteAll(options.output_path, output);
}

std::vector<std::string> FileOperationLogStore::LogPathsNewestFirst() const {
    std::vector<std::string> paths;
    paths.push_back(config_.operation_log_path);
    for (uint32_t i = 1; i <= config_.max_rotate_files; ++i) {
        paths.push_back(config_.operation_log_path + "." + std::to_string(i));
    }
    return paths;
}

infra::Status FileOperationLogStore::RotateIfNeededLocked() {
    infra::Result<uint64_t> size = infra::File::Size(config_.operation_log_path);
    if (!size.IsOk()) {
        return size.status == infra::Status::kNotFound ? infra::Status::kOk
                                                     : size.status;
    }
    if (size.value < config_.max_file_size_bytes) {
        return infra::Status::kOk;
    }

    if (config_.max_rotate_files == 0) {
        infra::File::Remove(config_.operation_log_path);
        return infra::Status::kOk;
    }

    const std::string last_path =
        config_.operation_log_path + "." + std::to_string(config_.max_rotate_files);
    if (infra::File::Exists(last_path)) {
        infra::File::Remove(last_path);
    }

    for (uint32_t i = config_.max_rotate_files; i > 1; --i) {
        const std::string from =
            config_.operation_log_path + "." + std::to_string(i - 1);
        const std::string to =
            config_.operation_log_path + "." + std::to_string(i);
        if (infra::File::Exists(from)) {
            const infra::Status rename_result = infra::File::Rename(from, to);
            if (rename_result != infra::Status::kOk) {
                return rename_result;
            }
        }
    }

    const infra::Status rename_result =
        infra::File::Rename(config_.operation_log_path,
                            config_.operation_log_path + ".1");
    if (rename_result != infra::Status::kOk &&
        rename_result != infra::Status::kNotFound) {
        return rename_result;
    }
    return infra::File::Append(config_.operation_log_path, "");
}

}  // namespace live_stream
