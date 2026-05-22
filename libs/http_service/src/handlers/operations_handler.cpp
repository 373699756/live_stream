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

}  // namespace

class OperationsHttpHandler : public IHttpHandler {
public:
    OperationsHttpHandler(HttpAccess *access,
                          ILoggerService *logger_service)
        : access_(access), logger_service_(logger_service) {}

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
        if (logger_service_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!access_->RequirePermission(request, AuthPermission::kManageUsers,
                                         "operations", &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        OperationLogQuery query;
        query.limit = 100;
        std::vector<OperationRecord> records =
            logger_service_->QueryOperations(query);
        ConfigJson root = ConfigJson::object();
        ConfigJson items = ConfigJson::array();
        for (const OperationRecord &record : records) {
            items.push_back(OperationRecordToJson(record));
        }
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpResponse HandleExport(const HttpRequest &request) {
        if (logger_service_ == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!access_->RequirePermission(request, AuthPermission::kManageUsers,
                                         "operations", &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        OperationLogQuery query;
        query.limit = 1000;
        std::vector<OperationRecord> records =
            logger_service_->QueryOperations(query);

        std::string body =
            "timestamp_ms,request_id,user_name,session_id,client_ip,module,"
            "action,target,result,reason\n";
        for (const OperationRecord &record : records) {
            body += std::to_string(record.timestamp_ms) + ",";
            body += record.request_id + ",";
            body += record.user_name + ",";
            body += record.session_id + ",";
            body += record.client_ip + ",";
            body += record.module + ",";
            body += OperationActionToString(record.action);
            body += ",";
            body += record.target + ",";
            body += OperationResultToString(record.result);
            body += ",";
            body += record.reason + "\n";
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
    ILoggerService *logger_service_ = nullptr;
};

std::unique_ptr<IHttpHandler> CreateOperationsHttpHandler(HttpAccess *access,
                            ILoggerService *logger_service) {
    return std::unique_ptr<IHttpHandler>(
        new OperationsHttpHandler(access, logger_service));
}

}  // namespace live_stream
