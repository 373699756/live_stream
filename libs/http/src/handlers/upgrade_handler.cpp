#include "handlers/http_handlers.h"

#include "http_auth_gate.h"
#include "http_json_body.h"
#include "http_query_string.h"
#include "http_response.h"

#include "http_protocol.h"
#include "infra/fs.h"
#include "infra/time.h"
#include "json_reader.h"
#include "system/package.h"
#include "system/upgrade.h"

#include <cctype>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace live_stream {
namespace {

constexpr const char *kUpgradeUploadDir = "/tmp/live_stream/upgrade/uploads";
constexpr std::size_t kMaxUpgradeUploadBytes = 32U * 1024U * 1024U;

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

bool WriteUploadFile(const std::string &path, const std::string &body) {
    if (path.empty() || body.empty() || body.size() > kMaxUpgradeUploadBytes) {
        return false;
    }
    const int fd = open(path.c_str(),
                        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) {
        return false;
    }
    std::size_t offset = 0;
    bool ok = true;
    while (offset < body.size()) {
        const ssize_t write_size =
            write(fd, body.data() + offset, body.size() - offset);
        if (write_size <= 0) {
            ok = false;
            break;
        }
        offset += static_cast<std::size_t>(write_size);
    }
    if (ok && fsync(fd) != 0) {
        ok = false;
    }
    close(fd);
    if (!ok) {
        static_cast<void>(infra::File::Remove(path));
    }
    return ok;
}

Json UpgradePackageInfoToJson(const UpgradePackageInfo &info) {
    Json root = Json::object();
    root["package_path"] = info.package_path;
    root["version"] = info.version;
    root["size_bytes"] = info.size_bytes;
    root["digest"] = info.digest;
    root["build_time_ms"] = info.build_time_ms;
    root["target_model"] = info.target_model;
    root["requires_reboot"] = info.requires_reboot;
    return root;
}

Json UpgradeInfoToJson(const UpgradeInfo &upgrade_info) {
    Json root = Json::object();
    root["state"] = UpgradeStateToString(upgrade_info.state);
    root["progress_percent"] = upgrade_info.progress_percent;
    root["current_stage"] = upgrade_info.current_stage;
    root["target_version"] = upgrade_info.target_version;
    root["ok"] = upgrade_info.ok;
    root["error_message"] = upgrade_info.error_message;
    root["started_at_ms"] = upgrade_info.started_at_ms;
    root["finished_at_ms"] = upgrade_info.finished_at_ms;
    return root;
}

HttpResponse RejectUploadedPackage(const HttpRequest &request,
                                   HttpAccess *access,
                                   const AuthPrincipal &principal,
                                   const std::string &upload_path,
                                   const std::string &msg) {
    static_cast<void>(infra::File::Remove(upload_path));
    if (access != nullptr) {
        access->RecordOperation(
            request, principal, OperationAction::kUpgrade, "upgrade",
            OperationResult::kRejected, msg);
    }
    return StatusResponse(400, msg);
}

bool ValidateWebUploadManifest(const std::string &upload_path,
                               std::string *msg) {
    UpgradeManifest manifest;
    std::string reason;
    if (!ReadUpgradePackageManifest(upload_path, &manifest, &reason)) {
        if (msg != nullptr) {
            *msg = reason.empty() ? "Could not validate package" : reason;
        }
        return false;
    }
    if (!UpgradePackageIsWebOnly(manifest)) {
        if (msg != nullptr) {
            *msg = "Web upgrade only accepts web partition packages";
        }
        return false;
    }
    if (msg != nullptr) {
        msg->clear();
    }
    return true;
}

bool UpgradeRequestFromJson(const Json &value, UpgradeRequest *request) {
    if (request == nullptr || !value.is_object()) {
        return false;
    }
    UpgradeRequest parsed;
    if (!json_reader::ReadField(value, "package_path", &parsed.package_path) ||
        !json_reader::ReadField(value, "expected_version", &parsed.expected_version) ||
        !json_reader::ReadField(value, "allow_same_version",
                               &parsed.allow_same_version) ||
        !json_reader::ReadField(value, "allow_downgrade", &parsed.allow_downgrade) ||
        !json_reader::ReadField(value, "auto_reboot", &parsed.auto_reboot)) {
        return false;
    }
    *request = parsed;
    return true;
}

}  // namespace

class UpgradeHttpHandler : public IHttpHandler {
public:
    explicit UpgradeHttpHandler(
        const UpgradeHandlerDependencies &dependencies)
        : access_(dependencies.access), upgrade_(dependencies.upgrade) {}

    void RegisterRoutes(IHttpRouter &router) override {
        if (upgrade_ == nullptr) {
            return;
        }
        router.AddExactRoute(HttpMethod::kPost, "/api/upgrade/upload",
                             this, &UpgradeHttpHandler::HandleUpload);
        router.AddExactRoute(HttpMethod::kGet, "/api/upgrade/status",
                             this, &UpgradeHttpHandler::HandleInfo);
        router.AddExactRoute(HttpMethod::kPost, "/api/upgrade/validate",
                             this, &UpgradeHttpHandler::HandleValidate);
        router.AddExactRoute(HttpMethod::kPost, "/api/upgrade/start",
                             this, &UpgradeHttpHandler::HandleStart);
        router.AddExactRoute(HttpMethod::kPost, "/api/upgrade/cancel",
                             this, &UpgradeHttpHandler::HandleCancel);
        router.AddExactRoute(HttpMethod::kPost,
                             "/api/upgrade/confirm-reboot", this,
                             &UpgradeHttpHandler::HandleConfirmReboot);
    }

private:
    HttpResponse HandleUpload(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kUpgrade, "upgrade",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (request.body.empty()) {
            return StatusResponse(400, "Empty package body");
        }
        if (request.body.size() > kMaxUpgradeUploadBytes) {
            return StatusResponse(413, "Package too large");
        }
        std::string file_name = QueryValue(request, "filename");
        if (file_name.empty()) {
            file_name =
                DecodeUrlComponent(GetHeader(request, "X-Upload-Filename"));
        }
        const std::string upload_path = UpgradeUploadPath(file_name);
        if (upload_path.empty()) {
            access_->RecordOperation(
                request, principal, OperationAction::kUpgrade, "upgrade",
                OperationResult::kRejected, "invalid upload filename");
            return StatusResponse(400, "Invalid upload filename");
        }
        if (!WriteUploadFile(upload_path, request.body)) {
            access_->RecordOperation(
                request, principal, OperationAction::kUpgrade, "upgrade",
                OperationResult::kFailed, "upload write failed");
            return StatusResponse(500, "Could not save upload");
        }

        std::string manifest_msg;
        if (!ValidateWebUploadManifest(upload_path, &manifest_msg)) {
            return RejectUploadedPackage(request, access_, principal,
                                         upload_path, manifest_msg);
        }

        const UpgradePackageInfo info =
            upgrade_->ValidatePackage(upload_path);
        if (info.version.empty()) {
            return RejectUploadedPackage(request, access_, principal,
                                         upload_path,
                                         "Could not validate package");
        }

        access_->RecordOperation(request, principal,
                                 OperationAction::kUpgrade, info.version,
                                 OperationResult::kSuccess,
                                 "package uploaded");
        return JsonResponse(200, UpgradePackageInfoToJson(info));
    }

    HttpResponse HandleInfo(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kReadStatus, "upgrade",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        return JsonResponse(200,
                            UpgradeInfoToJson(upgrade_->GetUpgradeInfo()));
    }

    HttpResponse HandleValidate(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kUpgrade, "upgrade",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        std::string package_path;
        if (!json_reader::ReadField(body, "package_path", &package_path)) {
            return StatusResponse(400, "Invalid upgrade request");
        }
        std::string manifest_msg;
        if (!ValidateWebUploadManifest(package_path, &manifest_msg)) {
            return StatusResponse(400, manifest_msg);
        }
        const UpgradePackageInfo info = upgrade_->ValidatePackage(package_path);
        if (info.version.empty()) {
            return StatusResponse(400, "Could not validate package");
        }
        return JsonResponse(200, UpgradePackageInfoToJson(info));
    }

    HttpResponse HandleStart(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kUpgrade, "upgrade",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        Json body;
        if (!ParseJsonObject(request, &body)) {
            return StatusResponse(400, "Invalid JSON");
        }
        UpgradeRequest upgrade_request;
        if (!UpgradeRequestFromJson(body, &upgrade_request)) {
            return StatusResponse(400, "Invalid upgrade request");
        }
        if (!upgrade_->StartUpgrade(access_->MakeContext(request, &principal),
                                    upgrade_request)) {
            return StatusResponse(409, "Could not start upgrade");
        }
        return JsonResponse(200,
                            UpgradeInfoToJson(upgrade_->GetUpgradeInfo()));
    }

    HttpResponse HandleCancel(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kUpgrade, "upgrade",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (!upgrade_->CancelUpgrade(access_->MakeContext(request,
                                                          &principal))) {
            return StatusResponse(409, "Could not cancel upgrade");
        }
        return JsonResponse(200,
                            UpgradeInfoToJson(upgrade_->GetUpgradeInfo()));
    }

    HttpResponse HandleConfirmReboot(const HttpRequest &request) {
        AuthPrincipal principal;
        HttpResponse auth_response = RequirePermissionResponse(
            access_, request, AuthPermission::kUpgrade, "upgrade",
            &principal);
        if (auth_response.status_code != 0) {
            return auth_response;
        }
        if (!upgrade_->ConfirmReboot(access_->MakeContext(request,
                                                          &principal))) {
            return StatusResponse(409, "Could not confirm reboot");
        }
        return JsonResponse(200,
                            UpgradeInfoToJson(upgrade_->GetUpgradeInfo()));
    }

    HttpAccess *access_ = nullptr;
    IUpgrade *upgrade_ = nullptr;
};

std::unique_ptr<IHttpHandler> MakeUpgradeHandler(
    const UpgradeHandlerDependencies &dependencies) {
    return std::unique_ptr<IHttpHandler>(
        new UpgradeHttpHandler(dependencies));
}

}  // namespace live_stream
