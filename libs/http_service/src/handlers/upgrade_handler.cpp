#include "handlers/http_handlers.h"

#include "http_handler_utils.h"

#include "http_protocol.h"
#include "http_request_utils.h"
#include "infra/fs.h"
#include "infra/time.h"
#include "live_stream/json_utils.h"
#include "upgrade_service.h"

#include <cctype>
#include <string>

namespace live_stream {
namespace {

constexpr const char *kUpgradeUploadDir = "/tmp/live_stream/upgrade/uploads";

bool IsSafeUploadNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' ||
           c == '-' || c == '_';
}

std::string SanitizeUploadFileName(const std::string &name) {
    if (name.empty() || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos ||
        name.find("..") != std::string::npos) {
        return std::string();
    }
    std::string sanitized;
    sanitized.reserve(name.size());
    for (char c : name) {
        sanitized.push_back(IsSafeUploadNameChar(c) ? c : '_');
    }
    while (!sanitized.empty() && sanitized.front() == '.') {
        sanitized.erase(sanitized.begin());
    }
    return sanitized;
}

std::string UpgradeUploadPath(const std::string &file_name) {
    const std::string sanitized = SanitizeUploadFileName(file_name);
    if (sanitized.empty()) {
        return std::string();
    }
    if (!infra::Path::MakeDirs(kUpgradeUploadDir)) {
        return std::string();
    }
    return infra::Path::Join(kUpgradeUploadDir,
                             std::to_string(infra::Time::SystemTimeMillis()) +
                                 "-" + sanitized);
}

bool RequireUpgradePermission(HttpHandlerContext *context,
                              const HttpRequest &request,
                              AuthPermission permission,
                              AuthPrincipal *principal) {
    return RequirePermissionOrForbidden(context, request, permission, "upgrade",
                                        principal);
}

ConfigJson UpgradePackageInfoToJson(const UpgradePackageInfo &info) {
    ConfigJson root = ConfigJson::object();
    root["package_path"] = info.package_path;
    root["version"] = info.version;
    root["size_bytes"] = info.size_bytes;
    root["digest"] = info.digest;
    root["build_time_ms"] = info.build_time_ms;
    root["target_model"] = info.target_model;
    root["requires_reboot"] = info.requires_reboot;
    return root;
}

ConfigJson UpgradeStatusToJson(const UpgradeStatus &status) {
    ConfigJson root = ConfigJson::object();
    root["state"] = UpgradeStateToString(status.state);
    root["progress_percent"] = status.progress_percent;
    root["current_stage"] = status.current_stage;
    root["target_version"] = status.target_version;
    root["ok"] = status.ok;
    root["error_message"] = status.error_message;
    root["started_at_ms"] = status.started_at_ms;
    root["finished_at_ms"] = status.finished_at_ms;
    return root;
}

bool UpgradeRequestFromJson(const ConfigJson &value, UpgradeRequest *request) {
    if (request == nullptr || !value.is_object()) {
        return false;
    }
    UpgradeRequest parsed;
    if (!json_utils::Load(value, "package_path", &parsed.package_path) ||
        !json_utils::Load(value, "expected_version", &parsed.expected_version) ||
        !json_utils::Load(value, "allow_same_version",
                          &parsed.allow_same_version) ||
        !json_utils::Load(value, "allow_downgrade", &parsed.allow_downgrade) ||
        !json_utils::Load(value, "auto_reboot", &parsed.auto_reboot)) {
        return false;
    }
    *request = parsed;
    return true;
}

}  // namespace

class UpgradeHttpHandler : public IHttpHandler {
public:
    UpgradeHttpHandler(HttpHandlerContext *context,
                       const DeviceHandlerDependencies &dependencies)
        : context_(context), dependencies_(dependencies) {}

    void RegisterRoutes(IHttpRouter *router) override {
        if (router == nullptr) {
            return;
        }
        router->AddExactRoute(HttpMethod::kPost, "/api/upgrade/upload",
                              &UpgradeHttpHandler::HandleUploadRoute, this);
        router->AddExactRoute(HttpMethod::kGet, "/api/upgrade/status",
                              &UpgradeHttpHandler::HandleStatusRoute, this);
        router->AddExactRoute(HttpMethod::kPost, "/api/upgrade/validate",
                              &UpgradeHttpHandler::HandleValidateRoute, this);
        router->AddExactRoute(HttpMethod::kPost, "/api/upgrade/start",
                              &UpgradeHttpHandler::HandleStartRoute, this);
        router->AddExactRoute(HttpMethod::kPost, "/api/upgrade/cancel",
                              &UpgradeHttpHandler::HandleCancelRoute, this);
        router->AddExactRoute(HttpMethod::kPost,
                              "/api/upgrade/confirm-reboot",
                              &UpgradeHttpHandler::HandleConfirmRebootRoute,
                              this);
    }

private:
    static HttpResponse HandleUploadRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<UpgradeHttpHandler *>(user)->HandleUpload(request);
    }

    static HttpResponse HandleStatusRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<UpgradeHttpHandler *>(user)->HandleStatus(request);
    }

    static HttpResponse HandleValidateRoute(void *user,
                                            const HttpRequest &request) {
        return static_cast<UpgradeHttpHandler *>(user)->HandleValidate(request);
    }

    static HttpResponse HandleStartRoute(void *user,
                                         const HttpRequest &request) {
        return static_cast<UpgradeHttpHandler *>(user)->HandleStart(request);
    }

    static HttpResponse HandleCancelRoute(void *user,
                                          const HttpRequest &request) {
        return static_cast<UpgradeHttpHandler *>(user)->HandleCancel(request);
    }

    static HttpResponse HandleConfirmRebootRoute(void *user,
                                                 const HttpRequest &request) {
        return static_cast<UpgradeHttpHandler *>(user)->HandleConfirmReboot(
            request);
    }

    HttpResponse HandleUpload(const HttpRequest &request) {
        IUpgradeService *upgrade_service = dependencies_.upgrade_service;
        if (upgrade_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireUpgradePermission(context_, request,
                                      AuthPermission::kUpgrade, &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        if (request.body.empty()) {
            return StatusResponse(400, "Empty package body");
        }
        std::string file_name = QueryValue(request, "filename");
        if (file_name.empty()) {
            file_name =
                DecodeUrlComponent(GetHeader(request, "X-Upload-Filename"));
        }
        const std::string upload_path = UpgradeUploadPath(file_name);
        if (upload_path.empty()) {
            context_->RecordOperation(
                request, principal, OperationAction::kUpgrade, "upgrade",
                OperationResult::kRejected, "invalid upload filename");
            return StatusResponse(400, "Invalid upload filename");
        }
        if (!infra::File::WriteAll(upload_path, request.body)) {
            context_->RecordOperation(
                request, principal, OperationAction::kUpgrade, "upgrade",
                OperationResult::kFailed, "upload write failed");
            return StatusResponse(500, "Could not store upload");
        }

        const UpgradePackageInfo info =
            upgrade_service->ValidatePackage(upload_path);
        if (info.version.empty()) {
            static_cast<void>(infra::File::Remove(upload_path));
            context_->RecordOperation(
                request, principal, OperationAction::kUpgrade, "upgrade",
                OperationResult::kRejected, "package validation failed");
            return StatusResponse(400, "Could not validate package");
        }

        context_->RecordOperation(request, principal,
                                  OperationAction::kUpgrade, info.version,
                                  OperationResult::kSuccess,
                                  "package uploaded");
        return JsonResponse(200, UpgradePackageInfoToJson(info));
    }

    HttpResponse HandleStatus(const HttpRequest &request) {
        IUpgradeService *upgrade_service = dependencies_.upgrade_service;
        if (upgrade_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireUpgradePermission(context_, request,
                                      AuthPermission::kReadStatus,
                                      &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        return JsonResponse(200,
                            UpgradeStatusToJson(upgrade_service->GetStatus()));
    }

    HttpResponse HandleValidate(const HttpRequest &request) {
        IUpgradeService *upgrade_service = dependencies_.upgrade_service;
        if (upgrade_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireUpgradePermission(context_, request,
                                      AuthPermission::kUpgrade, &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        ConfigJson body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        std::string package_path;
        if (!json_utils::Load(body, "package_path", &package_path)) {
            return StatusResponse(400, "Invalid upgrade request");
        }
        const UpgradePackageInfo info =
            upgrade_service->ValidatePackage(package_path);
        if (info.version.empty()) {
            return StatusResponse(400, "Could not validate package");
        }
        return JsonResponse(200, UpgradePackageInfoToJson(info));
    }

    HttpResponse HandleStart(const HttpRequest &request) {
        IUpgradeService *upgrade_service = dependencies_.upgrade_service;
        if (upgrade_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireUpgradePermission(context_, request,
                                      AuthPermission::kUpgrade, &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        ConfigJson body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        UpgradeRequest upgrade_request;
        if (!UpgradeRequestFromJson(body, &upgrade_request)) {
            return StatusResponse(400, "Invalid upgrade request");
        }
        if (!upgrade_service->StartUpgrade(
                context_->MakeContext(request, &principal), upgrade_request)) {
            return StatusResponse(409, "Could not start upgrade");
        }
        return JsonResponse(200,
                            UpgradeStatusToJson(upgrade_service->GetStatus()));
    }

    HttpResponse HandleCancel(const HttpRequest &request) {
        IUpgradeService *upgrade_service = dependencies_.upgrade_service;
        if (upgrade_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireUpgradePermission(context_, request,
                                      AuthPermission::kUpgrade, &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        if (!upgrade_service->CancelUpgrade(
                context_->MakeContext(request, &principal))) {
            return StatusResponse(409, "Could not cancel upgrade");
        }
        return JsonResponse(200,
                            UpgradeStatusToJson(upgrade_service->GetStatus()));
    }

    HttpResponse HandleConfirmReboot(const HttpRequest &request) {
        IUpgradeService *upgrade_service = dependencies_.upgrade_service;
        if (upgrade_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        if (!RequireUpgradePermission(context_, request,
                                      AuthPermission::kUpgrade, &principal)) {
            return StatusResponse(403, "Forbidden");
        }
        if (!upgrade_service->ConfirmReboot(
                context_->MakeContext(request, &principal))) {
            return StatusResponse(409, "Could not confirm reboot");
        }
        return JsonResponse(200,
                            UpgradeStatusToJson(upgrade_service->GetStatus()));
    }

    HttpHandlerContext *context_ = nullptr;
    DeviceHandlerDependencies dependencies_;
};

std::unique_ptr<IHttpHandler> CreateUpgradeHttpHandler(
    HttpHandlerContext *context, const DeviceHandlerDependencies &dependencies) {
    return std::unique_ptr<IHttpHandler>(
        new UpgradeHttpHandler(context, dependencies));
}

}  // namespace live_stream
