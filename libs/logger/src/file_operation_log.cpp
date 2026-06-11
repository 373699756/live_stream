/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: file_operation_log.cpp
 * Brief: Implements the JSON Lines file backend for operation audit records.
 */

#include "operation_log.h"

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

FileOperationLog::FileOperationLog(const LoggerConfig& config)
    : config_(config) {}

bool FileOperationLog::Open() {
    if (config_.operation_log_path.empty()) {
        return false;
    }
    if (config_.max_file_size_bytes == 0) {
        return false;
    }

    if (!infra::Path::MakeDirs(infra::Path::DirName(config_.operation_log_path))) {
        return false;
    }

    if (!infra::File::Append(config_.operation_log_path, "")) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    opened_ = true;
    return true;
}

void FileOperationLog::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    opened_ = false;
}

bool FileOperationLog::Append(const OperationRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!opened_) {
        return false;
    }

    if (!RotateIfNeededLocked()) {
        return false;
    }

    return infra::File::Append(config_.operation_log_path,
                               EncodeOperationRecord(record));
}

std::vector<OperationRecord> FileOperationLog::Query(
    const OperationLogQuery& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!opened_) {
        return {};
    }

    std::vector<OperationRecord> records;
    const std::vector<std::string> paths = LogPathsNewestFirst();
    for (const std::string& path : paths) {
        if (!infra::File::Exists(path)) {
            continue;
        }

        std::string content = infra::File::ReadAll(path);
        if (content.empty()) {
            continue;
        }

        std::vector<std::string> lines = SplitLines(content);
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
                return records;
            }
        }
    }

    return records;
}

bool FileOperationLog::Export(
    const OperationLogExportOptions& options) {
    if (options.output_path.empty()) {
        return false;
    }

    std::vector<OperationRecord> query_result = Query(options.query);

    if (!infra::Path::MakeDirs(infra::Path::DirName(options.output_path))) {
        return false;
    }

    std::string output;
    for (const OperationRecord& record : query_result) {
        output += EncodeOperationRecord(record);
    }
    return infra::File::WriteAll(options.output_path, output);
}

std::vector<std::string> FileOperationLog::LogPathsNewestFirst() const {
    std::vector<std::string> paths;
    paths.push_back(config_.operation_log_path);
    for (uint32_t i = 1; i <= config_.max_rotate_files; ++i) {
        paths.push_back(config_.operation_log_path + "." + std::to_string(i));
    }
    return paths;
}

bool FileOperationLog::RotateIfNeededLocked() {
    uint64_t size = infra::File::Size(config_.operation_log_path);
    if (size == 0 && !infra::File::Exists(config_.operation_log_path)) {
        return true;
    }
    if (size < config_.max_file_size_bytes) {
        return true;
    }

    if (config_.max_rotate_files == 0) {
        infra::File::Remove(config_.operation_log_path);
        return true;
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
            if (!infra::File::Rename(from, to)) {
                return false;
            }
        }
    }

    if (infra::File::Exists(config_.operation_log_path) &&
        !infra::File::Rename(config_.operation_log_path,
                             config_.operation_log_path + ".1")) {
        return false;
    }
    return infra::File::Append(config_.operation_log_path, "");
}

}  // namespace live_stream
