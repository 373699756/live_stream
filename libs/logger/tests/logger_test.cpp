#include "logger.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

void RemoveLogFiles(const std::string& path) {
    std::remove(path.c_str());
    for (int i = 1; i <= 4; ++i) {
        const std::string rotated = path + "." + std::to_string(i);
        std::remove(rotated.c_str());
    }
}

bool ReadFile(const std::string& path, std::string* content) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string output;
    std::string line;
    while (std::getline(file, line)) {
        output += line;
        output += '\n';
    }
    *content = output;
    return true;
}

}  // namespace

int main() {
    const std::string path = "/tmp/live_stream_operation_test.jsonl";
    const std::string export_path = "/tmp/live_stream_operation_export.jsonl";
    RemoveLogFiles(path);
    std::remove(export_path.c_str());

    live_stream::LoggerConfig config;
    config.operation_log_path = path;
    config.max_file_size_bytes = 1024U * 1024U;
    config.max_rotate_files = 4;

    std::unique_ptr<live_stream::ILogger> service =
        live_stream::CreateLogger(config);
    if (!service) {
        return 1;
    }
    live_stream::OperationRecord before_start;
    if (service->RecordOperation(before_start)) {
        return 3;
    }

    if (!service->Start()) {
        return 5;
    }

    live_stream::OperationRecord record;
    record.timestamp_ms = 1000;
    record.request_id = "req-1";
    record.user_name = "admin";
    record.session_id = "session-1";
    record.client_ip = "127.0.0.1";
    record.module = "auth";
    record.action = live_stream::OperationAction::kLogin;
    record.target = "session";
    record.result = live_stream::OperationResult::kSuccess;
    record.reason = "ok";

    if (!service->RecordOperation(record)) {
        return 6;
    }

    live_stream::OperationRecord failed = record;
    failed.timestamp_ms = 2000;
    failed.request_id = "req-2";
    failed.user_name = "operator";
    failed.action = live_stream::OperationAction::kReboot;
    failed.target = "device";
    failed.result = live_stream::OperationResult::kFailed;
    failed.reason = "denied";
    if (!service->RecordOperation(failed)) {
        return 7;
    }

    live_stream::OperationLogQuery query;
    query.limit = 10;
    std::vector<live_stream::OperationRecord> all =
        service->QueryOperations(query);
    if (all.size() != 2) {
        return 8;
    }
    if (all[0].request_id != "req-2" ||
        all[1].request_id != "req-1") {
        return 9;
    }

    live_stream::OperationLogQuery admin_query;
    admin_query.user_name = "admin";
    admin_query.has_action = true;
    admin_query.action = live_stream::OperationAction::kLogin;
    admin_query.has_result = true;
    admin_query.result = live_stream::OperationResult::kSuccess;
    std::vector<live_stream::OperationRecord> admin_records =
        service->QueryOperations(admin_query);
    if (admin_records.size() != 1 ||
        admin_records[0].request_id != "req-1") {
        return 10;
    }

    live_stream::OperationLogQuery range_query;
    range_query.begin_timestamp_ms = 1500;
    range_query.end_timestamp_ms = 2500;
    std::vector<live_stream::OperationRecord> range_records =
        service->QueryOperations(range_query);
    if (range_records.size() != 1 ||
        range_records[0].request_id != "req-2") {
        return 11;
    }

    live_stream::OperationLogExportOptions export_options;
    export_options.output_path = export_path;
    export_options.query = admin_query;
    if (!service->ExportOperations(export_options)) {
        return 12;
    }

    service->Stop();
    if (service->RecordOperation(record)) {
        return 13;
    }
    service->Stop();

    std::string log_content;
    if (!ReadFile(path, &log_content)) {
        return 14;
    }
    if (log_content.find("\"action\":\"Login\"") == std::string::npos ||
        log_content.find("\"user_name\":\"admin\"") == std::string::npos ||
        log_content.find("\"result\":\"Failed\"") == std::string::npos) {
        return 15;
    }

    std::string export_content;
    if (!ReadFile(export_path, &export_content)) {
        return 16;
    }
    if (export_content.find("\"request_id\":\"req-1\"") == std::string::npos ||
        export_content.find("\"request_id\":\"req-2\"") != std::string::npos) {
        return 17;
    }

    RemoveLogFiles(path);
    std::remove(export_path.c_str());
    return 0;
}
