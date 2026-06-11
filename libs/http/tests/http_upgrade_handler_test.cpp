#include "http_dependencies.h"

#include "auth.h"
#include "config.h"
#include "infra/fs.h"
#include "logger.h"
#include "net.h"
#include "upgrade.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakeAuth : public live_stream::IAuth {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    bool SetAuditSink(live_stream::IAuthAuditSink* sink) override {
        (void)sink;
        return true;
    }

    live_stream::LoginResult Login(
        const live_stream::LoginRequest& request) override {
        (void)request;
        return live_stream::LoginResult();
    }

    bool Logout(const live_stream::RequestContext& context) override {
        return !context.session_id.empty();
    }

    live_stream::TokenValidationResult ValidateToken(
        const std::string& token) override {
        live_stream::TokenValidationResult result;
        if (token == "admin-token") {
            result.principal.user_name = "admin";
            result.principal.session_id = "session-1";
            result.principal.role = live_stream::AuthRole::kAdmin;
        } else if (token == "viewer-token") {
            result.principal.user_name = "viewer";
            result.principal.session_id = "session-2";
            result.principal.role = live_stream::AuthRole::kViewer;
        }
        return result;
    }

    bool ChangePassword(
        const live_stream::ChangePasswordRequest& request) override {
        (void)request;
        return false;
    }

    bool CheckPermission(
        const live_stream::AuthPrincipal& principal,
        live_stream::AuthPermission permission,
        const std::string& target) override {
        (void)target;
        if (principal.role == live_stream::AuthRole::kAdmin) {
            return true;
        }
        return permission == live_stream::AuthPermission::kReadStatus;
    }
};

class FakeLogger : public live_stream::ILogger {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    bool RecordOperation(
        const live_stream::OperationRecord& record) override {
        records.push_back(record);
        return true;
    }

    std::vector<live_stream::OperationRecord> QueryOperations(
        const live_stream::OperationLogQuery& query) override {
        (void)query;
        return records;
    }

    bool ExportOperations(
        const live_stream::OperationLogExportOptions& options) override {
        (void)options;
        return true;
    }

    std::vector<live_stream::OperationRecord> records;
};

class FakeUpgrade : public live_stream::IUpgrade {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    live_stream::UpgradeStatus GetStatus() override { return status; }

    live_stream::UpgradePackageInfo ValidatePackage(
        const std::string& package_path) override {
        ++validate_calls;
        last_validate_path = package_path;
        if (!validate_ok) {
            return live_stream::UpgradePackageInfo();
        }
        live_stream::UpgradePackageInfo info;
        info.package_path = package_path;
        info.version = "2.0.0";
        info.size_bytes = 4;
        info.digest = "abc";
        info.target_model = "Hi3516DV300";
        info.requires_reboot = true;
        return info;
    }

    bool StartUpgrade(const live_stream::RequestContext& context,
                      const live_stream::UpgradeRequest& request) override {
        ++start_calls;
        last_context = context;
        last_request = request;
        return start_ok;
    }

    bool CancelUpgrade(
        const live_stream::RequestContext& context) override {
        ++cancel_calls;
        last_context = context;
        return cancel_ok;
    }

    bool ConfirmReboot(
        const live_stream::RequestContext& context) override {
        ++confirm_reboot_calls;
        last_context = context;
        return confirm_reboot_ok;
    }

    live_stream::UpgradeStatus status;
    live_stream::RequestContext last_context;
    live_stream::UpgradeRequest last_request;
    std::string last_validate_path;
    int validate_calls = 0;
    int start_calls = 0;
    int cancel_calls = 0;
    int confirm_reboot_calls = 0;
    bool validate_ok = true;
    bool start_ok = true;
    bool cancel_ok = true;
    bool confirm_reboot_ok = true;
};

class FakeNetEngine : public live_stream::INetEngine {
public:
    class FakeNetExecutor : public live_stream::INetExecutor {
    public:
        bool Post(infra::Task task) override {
            if (task) {
                task();
            }
            return true;
        }

        live_stream::NetTimerId RunAfter(uint32_t, infra::Task task) override {
            if (task) {
                task();
            }
            return 1;
        }

        live_stream::NetTimerId RunEvery(uint32_t, infra::Task) override {
            return 1;
        }

        bool CancelTimer(live_stream::NetTimerId) override { return true; }
        bool IsCurrentThread() const override { return true; }
    };

    bool Start() override { return true; }
    void Stop() override {}

    live_stream::INetExecutor* DefaultExecutor() override {
        return &executor_;
    }

    live_stream::INetExecutor* PickExecutor() override {
        return &executor_;
    }

    live_stream::TcpServerId ListenTcp(
        live_stream::INetExecutor*,
        const live_stream::TcpListenOptions& options,
        const live_stream::TcpCallbacks& callbacks) override {
        (void)options;
        (void)callbacks;
        return 1;
    }

    bool CloseTcp(live_stream::TcpServerId id) override {
        (void)id;
        return true;
    }

    live_stream::UdpSocketId BindUdp(
        live_stream::INetExecutor*,
        const live_stream::UdpBindOptions& options,
        const live_stream::UdpCallbacks& callbacks) override {
        (void)options;
        (void)callbacks;
        return 1;
    }

    bool CloseUdp(live_stream::UdpSocketId id) override {
        (void)id;
        return true;
    }

    bool Send(live_stream::ConnectionId id,
              const uint8_t* data,
              size_t size) override {
        (void)id;
        (void)data;
        (void)size;
        return true;
    }

    bool Close(live_stream::ConnectionId id) override {
        (void)id;
        return true;
    }

    bool CloseAfterSend(live_stream::ConnectionId id) override {
        (void)id;
        return true;
    }

    bool SendTo(live_stream::UdpSocketId id,
                live_stream::NetAddress address,
                const uint8_t* data,
                size_t size) override {
        (void)id;
        (void)address;
        (void)data;
        (void)size;
        return true;
    }

    bool SetUdpPeer(live_stream::UdpSocketId id,
                    live_stream::NetAddress peer) override {
        (void)id;
        (void)peer;
        return true;
    }

    bool SendToPeer(live_stream::UdpSocketId id,
                    const uint8_t* data,
                    size_t size) override {
        (void)id;
        (void)data;
        (void)size;
        return true;
    }

    live_stream::NetAddress TcpLocalAddress(
        live_stream::TcpServerId id) const override {
        (void)id;
        live_stream::NetAddress address;
        address.ip = "127.0.0.1";
        address.port = 80;
        return address;
    }

    live_stream::NetAddress UdpLocalAddress(
        live_stream::UdpSocketId id) const override {
        (void)id;
        live_stream::NetAddress address;
        address.ip = "127.0.0.1";
        address.port = 80;
        return address;
    }

    live_stream::NetAddress UdpPeerAddress(
        live_stream::UdpSocketId id) const override {
        (void)id;
        live_stream::NetAddress address;
        address.ip = "127.0.0.1";
        address.port = 40000;
        return address;
    }

    uint32_t PendingBytes(live_stream::ConnectionId id) const override {
        (void)id;
        return 0;
    }

    live_stream::NetStats GetStats() const override {
        return live_stream::NetStats();
    }

private:
    FakeNetExecutor executor_;
};

live_stream::HttpRequest Request(live_stream::HttpMethod method,
                                 const std::string& path,
                                 const std::string& query,
                                 const std::string& body,
                                 const std::string& token) {
    live_stream::HttpRequest request;
    request.method = method;
    request.path = path;
    request.query_string = query;
    request.body = body;
    request.client_ip = "192.0.2.10";
    request.headers["User-Agent"] = "unit-test";
    if (!token.empty()) {
        request.headers["Authorization"] = "Bearer " + token;
    }
    return request;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::unique_ptr<live_stream::IHttp> MakeHttp(
    FakeNetEngine* net_engine,
    FakeAuth* auth,
    FakeLogger* logger,
    FakeUpgrade* upgrade) {
    live_stream::HttpOptions options;
    live_stream::HttpDependencies dependencies;
    dependencies.net_engine = net_engine;
    dependencies.net_executor =
        net_engine == nullptr ? nullptr : net_engine->DefaultExecutor();
    dependencies.auth = auth;
    dependencies.logger = logger;
    dependencies.upgrade = upgrade;
    return live_stream::CreateHttp(options, dependencies);
}

int TestUpgradeUploadAndStart() {
    FakeNetEngine net_engine;
    FakeAuth auth;
    FakeLogger logger;
    FakeUpgrade upgrade;
    std::unique_ptr<live_stream::IHttp> http =
        MakeHttp(&net_engine, &auth, &logger, &upgrade);
    if (!http || !http->Start()) {
        return 1;
    }

    live_stream::HttpResponse upload = http->HandleRequest(
        Request(live_stream::HttpMethod::kPost, "/api/upgrade/upload",
                "filename=../bad%20name.zip", "PK00", "admin-token"));
    if (upload.status_code != 400 || upgrade.validate_calls != 0) {
        return 2;
    }

    upload = http->HandleRequest(
        Request(live_stream::HttpMethod::kPost, "/api/upgrade/upload",
                "filename=upgrade.zip", "PK00", "viewer-token"));
    if (upload.status_code != 403 || upgrade.validate_calls != 0) {
        return 3;
    }

    upload = http->HandleRequest(
        Request(live_stream::HttpMethod::kPost, "/api/upgrade/upload",
                "filename=upgrade.zip", "PK00", "admin-token"));
    if (upload.status_code != 200 || upgrade.validate_calls != 1 ||
        !Contains(upgrade.last_validate_path,
                  "/tmp/live_stream/upgrade/uploads/") ||
        !Contains(upload.body, "\"version\":\"2.0.0\"")) {
        return 4;
    }

    const std::string start_body =
        "{\"package_path\":\"" + upgrade.last_validate_path +
        "\",\"expected_version\":\"2.0.0\",\"allow_same_version\":false,"
        "\"allow_downgrade\":false,\"auto_reboot\":true}";
    live_stream::HttpResponse start = http->HandleRequest(
        Request(live_stream::HttpMethod::kPost, "/api/upgrade/start", "",
                start_body, "admin-token"));
    if (start.status_code != 200 || upgrade.start_calls != 1 ||
        upgrade.last_request.package_path != upgrade.last_validate_path ||
        upgrade.last_request.expected_version != "2.0.0" ||
        !upgrade.last_request.auto_reboot ||
        upgrade.last_context.user_name != "admin") {
        return 5;
    }

    static_cast<void>(infra::File::Remove(upgrade.last_validate_path));
    http->Stop();
    return 0;
}

int TestUpgradeStatusValidateCancelAndReboot() {
    FakeNetEngine net_engine;
    FakeAuth auth;
    FakeLogger logger;
    FakeUpgrade upgrade;
    upgrade.status.state = live_stream::UpgradeState::kWaitingReboot;
    upgrade.status.progress_percent = 100;
    upgrade.status.current_stage = "waiting_reboot";
    upgrade.status.target_version = "2.0.0";
    std::unique_ptr<live_stream::IHttp> http =
        MakeHttp(&net_engine, &auth, &logger, &upgrade);
    if (!http || !http->Start()) {
        return 1;
    }

    live_stream::HttpResponse status = http->HandleRequest(
        Request(live_stream::HttpMethod::kGet, "/api/upgrade/status", "", "",
                "viewer-token"));
    if (status.status_code != 200 ||
        !Contains(status.body, "\"state\":\"waiting_reboot\"")) {
        return 2;
    }

    live_stream::HttpResponse validate = http->HandleRequest(
        Request(live_stream::HttpMethod::kPost, "/api/upgrade/validate", "",
                "{\"package_path\":\"/tmp/live_stream/upgrade/uploads/a.zip\"}",
                "admin-token"));
    if (validate.status_code != 200 || upgrade.validate_calls != 1 ||
        upgrade.last_validate_path != "/tmp/live_stream/upgrade/uploads/a.zip") {
        return 3;
    }

    live_stream::HttpResponse invalid_start = http->HandleRequest(
        Request(live_stream::HttpMethod::kPost, "/api/upgrade/start", "",
                "{\"package_path\":\"/tmp/live_stream/upgrade/uploads/a.zip\"}",
                "admin-token"));
    if (invalid_start.status_code != 400 || upgrade.start_calls != 0) {
        return 4;
    }

    live_stream::HttpResponse cancel = http->HandleRequest(
        Request(live_stream::HttpMethod::kPost, "/api/upgrade/cancel", "", "",
                "admin-token"));
    if (cancel.status_code != 200 || upgrade.cancel_calls != 1) {
        return 5;
    }

    live_stream::HttpResponse reboot = http->HandleRequest(
        Request(live_stream::HttpMethod::kPost,
                "/api/upgrade/confirm-reboot", "", "", "admin-token"));
    if (reboot.status_code != 200 || upgrade.confirm_reboot_calls != 1) {
        return 6;
    }

    http->Stop();
    return 0;
}

}  // namespace

int main() {
    if (TestUpgradeUploadAndStart() != 0) {
        return 1;
    }
    if (TestUpgradeStatusValidateCancelAndReboot() != 0) {
        return 2;
    }
    return 0;
}
