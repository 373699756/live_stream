#include "auth.h"

namespace live_stream {

const char* AuthRoleToString(AuthRole role) {
    switch (role) {
        case AuthRole::kAdmin:
            return "Admin";
        case AuthRole::kOperator:
            return "Operator";
        case AuthRole::kViewer:
            return "Viewer";
    }
    return "Unknown";
}

const char* AuthPermissionToString(AuthPermission permission) {
    switch (permission) {
        case AuthPermission::kReadStatus:
            return "ReadStatus";
        case AuthPermission::kPreviewVideo:
            return "PreviewVideo";
        case AuthPermission::kModifyConfig:
            return "ModifyConfig";
        case AuthPermission::kUpgrade:
            return "Upgrade";
        case AuthPermission::kReboot:
            return "Reboot";
        case AuthPermission::kFactoryReset:
            return "FactoryReset";
        case AuthPermission::kManageUsers:
            return "ManageUsers";
    }
    return "Unknown";
}

const char* AuthAuditActionToString(AuthAuditAction action) {
    switch (action) {
        case AuthAuditAction::kLogin:
            return "Login";
        case AuthAuditAction::kLogout:
            return "Logout";
        case AuthAuditAction::kAuthFailed:
            return "AuthFailed";
        case AuthAuditAction::kTokenExpired:
            return "TokenExpired";
        case AuthAuditAction::kPermissionDenied:
            return "PermissionDenied";
    }
    return "Unknown";
}

const char* AuthAuditResultToString(AuthAuditResult result) {
    switch (result) {
        case AuthAuditResult::kSuccess:
            return "Success";
        case AuthAuditResult::kFailed:
            return "Failed";
        case AuthAuditResult::kRejected:
            return "Rejected";
    }
    return "Unknown";
}

}  // namespace live_stream
