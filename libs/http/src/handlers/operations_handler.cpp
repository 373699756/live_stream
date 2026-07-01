#include "handlers/http_handlers.h"

#include "http_auth_gate.h"
#include "http_response.h"

#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

Json OperationRecordToJson(const OperationRecord &record) {
    Json root = Json::object();
    root["timestamp_ms"] = record.timestamp_ms;
    root["request_id"] = record.request_id;
    root["user_name"] = record.user_name;
    root["session_id"] = record.session_id;
    root["client_ip"] = record.client_ip;
    root["module"] = record.module;
    root["action"] = OperationActionToString(record.action);
    root["target"] = record.target;
    root["result"] = OperationResultToString(record.result);
    root["reason"] = record.reason;
    return root;
}

bool NeedsSpreadsheetLiteralPrefix(const std::string &value) {
    for (char c : value) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }
        return c == '=' || c == '+' || c == '-' || c == '@';
    }
    return false;
}

std::string CsvField(const std::string &value) {
    std::string cell;
    cell.reserve(value.size() + 4);
    if (NeedsSpreadsheetLiteralPrefix(value)) {
        cell.push_back('\'');
    }
    cell += value;

    std::string escaped;
    escaped.reserve(cell.size() + 2);
    escaped.push_back('"');
    for (char c : cell) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(c);
        }
    }
    escaped.push_back('"');
    return escaped;
}

void AppendCsvRow(const OperationRecord &record, std::string *body) {
    if (body == nullptr) {
        return;
    }
    *body += std::to_string(record.timestamp_ms);
    const std::string fields[] = {
        record.request_id,
        record.user_name,
        record.session_id,
        record.client_ip,
        record.module,
        OperationActionToString(record.action),
        record.target,
        OperationResultToString(record.result),
        record.reason,
    };
    for (const std::string &field : fields) {
        *body += ",";
        *body += CsvField(field);
    }
    *body += "\n";
}

}  // namespace

class OperationsHttpHandler : public IHttpHandler {
public:
    explicit OperationsHttpHandler(
        const OperationsHandlerRefs &refs)
        : access_(refs.access), logger_(refs.logger) {}

    void RegisterRoutes(IHttpRouter &router) override {
        if (logger_ == nullptr) {
            return;
        }
        router.AddExactRoute(HttpMethod::kGet, "/api/operations/export",
                             this, &OperationsHttpHandler::HandleExport);
        router.AddExactRoute(HttpMethod::kGet, "/api/operations",
                             this, &OperationsHttpHandler::HandleOperations);
    }

private:
    HttpResponse HandleOperations(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kManageUsers, "operations",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        OperationLogQuery query;
        query.limit = 100;
        std::vector<OperationRecord> records =
            logger_->QueryOperations(query);
        Json root = Json::object();
        Json items = Json::array();
        for (const OperationRecord &record : records) {
            items.push_back(OperationRecordToJson(record));
        }
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpResponse HandleExport(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kManageUsers, "operations",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        OperationLogQuery query;
        query.limit = 1000;
        std::vector<OperationRecord> records =
            logger_->QueryOperations(query);

        std::string body =
            "timestamp_ms,request_id,user_name,session_id,client_ip,module,"
            "action,target,result,reason\n";
        for (const OperationRecord &record : records) {
            AppendCsvRow(record, &body);
        }

        HttpResponse response;
        response.status_code = 200;
        response.headers["Content-Type"] = "text/csv";
        response.headers["Content-Disposition"] =
            "attachment; filename=\"operations.csv\"";
        response.body = std::move(body);
        return response;
    }

    HttpAccess *access_ = nullptr;
    ILogger *logger_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeOperationsHandler(
    const OperationsHandlerRefs &refs) {
    return std::unique_ptr<IHttpHandler>(
        new OperationsHttpHandler(refs));
}

}  // namespace live_stream
