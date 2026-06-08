/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: operation_record_codec.cpp
 * Brief: Implements JSON Lines encoding and decoding for operation records.
 */

#include "operation_record_codec.h"

#include <cstdlib>

namespace live_stream {
namespace {

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += c;
                break;
        }
    }
    return escaped;
}

bool FindStringValue(const std::string& line,
                     const char* key,
                     std::string* value) {
    const std::string marker = std::string("\"") + key + "\":\"";
    const size_t start = line.find(marker);
    if (start == std::string::npos) {
        return false;
    }

    size_t pos = start + marker.size();
    std::string result;
    while (pos < line.size()) {
        const char c = line[pos++];
        if (c == '"') {
            *value = result;
            return true;
        }
        if (c == '\\' && pos < line.size()) {
            const char escaped = line[pos++];
            switch (escaped) {
                case '\\':
                    result += '\\';
                    break;
                case '"':
                    result += '"';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                default:
                    result += escaped;
                    break;
            }
        } else {
            result += c;
        }
    }

    return false;
}

bool FindInt64Value(const std::string& line, const char* key, int64_t* value) {
    const std::string marker = std::string("\"") + key + "\":";
    const size_t start = line.find(marker);
    if (start == std::string::npos) {
        return false;
    }

    const char* begin = line.c_str() + start + marker.size();
    char* end = nullptr;
    const long long parsed = std::strtoll(begin, &end, 10);
    if (end == begin) {
        return false;
    }
    *value = static_cast<int64_t>(parsed);
    return true;
}

}  // namespace

const char* OperationActionToString(OperationAction action) {
    switch (action) {
        case OperationAction::kLogin:
            return "Login";
        case OperationAction::kLogout:
            return "Logout";
        case OperationAction::kAuthFailed:
            return "AuthFailed";
        case OperationAction::kTokenExpired:
            return "TokenExpired";
        case OperationAction::kModifyConfig:
            return "ModifyConfig";
        case OperationAction::kReboot:
            return "Reboot";
        case OperationAction::kFactoryReset:
            return "FactoryReset";
        case OperationAction::kUpgrade:
            return "Upgrade";
        case OperationAction::kTimeSync:
            return "TimeSync";
        case OperationAction::kNetworkChange:
            return "NetworkChange";
        case OperationAction::kUserManage:
            return "UserManage";
        case OperationAction::kPermissionDenied:
            return "PermissionDenied";
    }
    return "Unknown";
}

const char* OperationResultToString(OperationResult result) {
    switch (result) {
        case OperationResult::kSuccess:
            return "Success";
        case OperationResult::kFailed:
            return "Failed";
        case OperationResult::kRejected:
            return "Rejected";
    }
    return "Unknown";
}

bool OperationActionFromString(const std::string& value,
                               OperationAction* action) {
    if (action == nullptr) {
        return false;
    }
    if (value == "Login") {
        *action = OperationAction::kLogin;
    } else if (value == "Logout") {
        *action = OperationAction::kLogout;
    } else if (value == "AuthFailed") {
        *action = OperationAction::kAuthFailed;
    } else if (value == "TokenExpired") {
        *action = OperationAction::kTokenExpired;
    } else if (value == "ModifyConfig") {
        *action = OperationAction::kModifyConfig;
    } else if (value == "Reboot") {
        *action = OperationAction::kReboot;
    } else if (value == "FactoryReset") {
        *action = OperationAction::kFactoryReset;
    } else if (value == "Upgrade") {
        *action = OperationAction::kUpgrade;
    } else if (value == "TimeSync") {
        *action = OperationAction::kTimeSync;
    } else if (value == "NetworkChange") {
        *action = OperationAction::kNetworkChange;
    } else if (value == "UserManage") {
        *action = OperationAction::kUserManage;
    } else if (value == "PermissionDenied") {
        *action = OperationAction::kPermissionDenied;
    } else {
        return false;
    }
    return true;
}

bool OperationResultFromString(const std::string& value,
                               OperationResult* result) {
    if (result == nullptr) {
        return false;
    }
    if (value == "Success") {
        *result = OperationResult::kSuccess;
    } else if (value == "Failed") {
        *result = OperationResult::kFailed;
    } else if (value == "Rejected") {
        *result = OperationResult::kRejected;
    } else {
        return false;
    }
    return true;
}

std::string EncodeOperationRecord(const OperationRecord& record) {
    std::string line;
    line += "{\"timestamp_ms\":";
    line += std::to_string(record.timestamp_ms);
    line += ",\"request_id\":\"";
    line += EscapeJson(record.request_id);
    line += "\",\"user_name\":\"";
    line += EscapeJson(record.user_name);
    line += "\",\"session_id\":\"";
    line += EscapeJson(record.session_id);
    line += "\",\"client_ip\":\"";
    line += EscapeJson(record.client_ip);
    line += "\",\"module\":\"";
    line += EscapeJson(record.module);
    line += "\",\"action\":\"";
    line += OperationActionToString(record.action);
    line += "\",\"target\":\"";
    line += EscapeJson(record.target);
    line += "\",\"result\":\"";
    line += OperationResultToString(record.result);
    line += "\",\"reason\":\"";
    line += EscapeJson(record.reason);
    line += "\"}\n";
    return line;
}

bool DecodeOperationRecord(const std::string& line, OperationRecord* record) {
    if (record == nullptr) {
        return false;
    }

    OperationRecord decoded;
    std::string action;
    std::string result;
    if (!FindInt64Value(line, "timestamp_ms", &decoded.timestamp_ms) ||
        !FindStringValue(line, "request_id", &decoded.request_id) ||
        !FindStringValue(line, "user_name", &decoded.user_name) ||
        !FindStringValue(line, "session_id", &decoded.session_id) ||
        !FindStringValue(line, "client_ip", &decoded.client_ip) ||
        !FindStringValue(line, "module", &decoded.module) ||
        !FindStringValue(line, "action", &action) ||
        !FindStringValue(line, "target", &decoded.target) ||
        !FindStringValue(line, "result", &result) ||
        !FindStringValue(line, "reason", &decoded.reason)) {
        return false;
    }

    if (!OperationActionFromString(action, &decoded.action) ||
        !OperationResultFromString(result, &decoded.result)) {
        return false;
    }

    *record = decoded;
    return true;
}

}  // namespace live_stream
