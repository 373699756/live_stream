#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

ConfigJson OperationRecordToJson(const OperationRecord &record) {
    ConfigJson root = ConfigJson::object();
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
    *body += ",";
    *body += CsvField(record.request_id);
    *body += ",";
    *body += CsvField(record.user_name);
    *body += ",";
    *body += CsvField(record.session_id);
    *body += ",";
    *body += CsvField(record.client_ip);
    *body += ",";
    *body += CsvField(record.module);
    *body += ",";
    *body += CsvField(OperationActionToString(record.action));
    *body += ",";
    *body += CsvField(record.target);
    *body += ",";
    *body += CsvField(OperationResultToString(record.result));
    *body += ",";
    *body += CsvField(record.reason);
    *body += "\n";
}

}  // namespace

class OperationsHttpHandler : public IHttpHandler {
public:
    OperationsHttpHandler(HttpAccess *access,
                          ILogger *logger)
        : access_(access), logger_(logger) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kGet, "/api/operations/export",
                              &OperationsHttpHandler::HandleExportRoute, this);
        router->AddExactRoute(HttpMethod::kGet, "/api/operations",
                              &OperationsHttpHandler::HandleOperationsRoute,
                              this);
    }

private:
    static HttpResponse HandleOperationsRoute(void *user,
                                              const HttpRequest &request) {
        return static_cast<OperationsHttpHandler *>(user)->HandleOperations(
            request);
    }

    static HttpResponse HandleExportRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<OperationsHttpHandler *>(user)->HandleExport(
            request);
    }

    HttpResponse HandleOperations(const HttpRequest &request) {
        if (logger_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!access_->RequirePermission(request, AuthPermission::kManageUsers,
                                        "operations", &principal)) {
            return ForbiddenResponse(principal);
        }
        OperationLogQuery query;
        query.limit = 100;
        std::vector<OperationRecord> records =
            logger_->QueryOperations(query);
        ConfigJson root = ConfigJson::object();
        ConfigJson items = ConfigJson::array();
        for (const OperationRecord &record : records) {
            items.push_back(OperationRecordToJson(record));
        }
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpResponse HandleExport(const HttpRequest &request) {
        if (logger_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!access_->RequirePermission(request, AuthPermission::kManageUsers,
                                        "operations", &principal)) {
            return ForbiddenResponse(principal);
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

std::unique_ptr<IHttpHandler> MakeOperationsHandler(HttpAccess *access,
                                                    ILogger *logger) {
    return std::unique_ptr<IHttpHandler>(
        new OperationsHttpHandler(access, logger));
}

}  // namespace live_stream
