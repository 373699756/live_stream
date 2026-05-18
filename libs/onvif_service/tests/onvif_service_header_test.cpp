#include "onvif_service.h"

#include "auth_service.h"
#include "event_service.h"
#include "net_service.h"
#include "system_service.h"
#include "time_service.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakeNetEngine : public live_stream::NetEngine {
public:
    bool Start() override { return true; }

    void Stop() override {}

    live_stream::TcpServerId ListenTcp(
        const live_stream::TcpListenOptions&,
        const live_stream::TcpCallbacks& callbacks) override {
        ++listen_tcp_count;
        tcp_callbacks = callbacks;
        return listen_tcp_ok ? 10 : 0;
    }

    bool CloseTcp(live_stream::TcpServerId) override {
        ++close_tcp_count;
        return true;
    }

    live_stream::UdpSocketId BindUdp(
        const live_stream::UdpBindOptions&,
        const live_stream::UdpCallbacks& callbacks) override {
        ++bind_udp_count;
        udp_callbacks = callbacks;
        return bind_udp_ok ? 20 : 0;
    }

    bool CloseUdp(live_stream::UdpSocketId) override {
        ++close_udp_count;
        return true;
    }

    bool Send(live_stream::ConnectionId,
              const uint8_t* data,
              size_t size) override {
        last_tcp_send.assign(reinterpret_cast<const char*>(data), size);
        ++send_count;
        return true;
    }

    bool Close(live_stream::ConnectionId) override {
        return true;
    }

    bool CloseAfterSend(live_stream::ConnectionId) override {
        ++close_after_send_count;
        return true;
    }

    bool SendTo(live_stream::UdpSocketId,
                live_stream::NetAddress address,
                const uint8_t* data,
                size_t size) override {
        last_udp_peer = address;
        last_udp_send.assign(reinterpret_cast<const char*>(data), size);
        ++send_to_count;
        return true;
    }

    live_stream::NetTimerId RunOnIoAfter(uint32_t, infra::Task) override {
        return 1;
    }

    bool CancelIoTimer(live_stream::NetTimerId) override {
        return true;
    }

    live_stream::NetAddress TcpLocalAddress(
        live_stream::TcpServerId) const override {
        return live_stream::NetAddress{"127.0.0.1", 8000};
    }

    live_stream::NetAddress UdpLocalAddress(
        live_stream::UdpSocketId) const override {
        return live_stream::NetAddress{"127.0.0.1", 3702};
    }

    uint32_t PendingBytes(live_stream::ConnectionId) const override {
        return 0;
    }

    live_stream::NetStats GetStats() const override {
        return live_stream::NetStats();
    }

    void DeliverTcp(const std::string& request) {
        tcp_callbacks.on_read(tcp_callbacks.user, 33,
                              reinterpret_cast<const uint8_t*>(request.data()),
                              request.size());
    }

    void DeliverUdp(const std::string& request) {
        live_stream::NetAddress peer{"192.0.2.44", 3702};
        udp_callbacks.on_read(udp_callbacks.user, 20, peer,
                              reinterpret_cast<const uint8_t*>(request.data()),
                              request.size());
    }

    live_stream::TcpCallbacks tcp_callbacks;
    live_stream::UdpCallbacks udp_callbacks;
    live_stream::NetAddress last_udp_peer;
    std::string last_tcp_send;
    std::string last_udp_send;
    bool listen_tcp_ok = true;
    bool bind_udp_ok = true;
    int listen_tcp_count = 0;
    int bind_udp_count = 0;
    int close_tcp_count = 0;
    int close_udp_count = 0;
    int send_count = 0;
    int send_to_count = 0;
    int close_after_send_count = 0;
};

class FakeEventService : public live_stream::IEventService {
public:
    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}

    live_stream::EventSubscriptionId Subscribe(
        live_stream::EventType, live_stream::EventHandler) override {
        return 1;
    }

    bool Unsubscribe(live_stream::EventSubscriptionId) override {
        return true;
    }

    bool Publish(const live_stream::Event& event) override {
        ++publish_count;
        last_event = event;
        return true;
    }

    int publish_count = 0;
    live_stream::Event last_event;
};

class FakeSystemService : public live_stream::ISystemService {
public:
    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}

    live_stream::DeviceInfo GetDeviceInfo() override {
        return info;
    }

    live_stream::SystemStatus GetSystemStatus() override {
        return live_stream::SystemStatus();
    }

    live_stream::SystemCapabilities GetCapabilities() override {
        return live_stream::SystemCapabilities();
    }

    bool Reboot(const live_stream::RequestContext&) override {
        return true;
    }

    bool FactoryReset(const live_stream::RequestContext&) override {
        return true;
    }

    bool ReportHeartbeat(const std::string&) override {
        return true;
    }

    live_stream::DeviceInfo info{"model-x", "serial-1", "fw-1"};
};

class FakeTimeService : public live_stream::ITimeService {
public:
    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}

    live_stream::TimeStatus GetTimeStatus() override {
        return status;
    }

    bool SetTimezone(const live_stream::RequestContext&,
                     const std::string&) override {
        return true;
    }

    bool SetSystemTime(const live_stream::RequestContext& context,
                       int64_t unix_time_ms,
                       live_stream::TimeSyncSource source) override {
        last_context = context;
        last_set_time_ms = unix_time_ms;
        last_source = source;
        ++set_count;
        return set_status;
    }

    bool SyncNow(const live_stream::RequestContext&,
                 live_stream::TimeSyncSource) override {
        return true;
    }

    bool UpdateNtpConfig(const live_stream::RequestContext&,
                         const live_stream::NtpConfig&) override {
        return true;
    }

    live_stream::TimeStatus status;
    live_stream::RequestContext last_context;
    live_stream::TimeSyncSource last_source = live_stream::TimeSyncSource::kManual;
    bool set_status = true;
    int64_t last_set_time_ms = 0;
    int set_count = 0;
};

class FakeUriProvider : public live_stream::IOnvifUriProvider {
public:
    std::string GetStreamUri(live_stream::StreamId stream_id) override {
        ++stream_count;
        last_stream_id = stream_id;
        if (!stream_ok) {
            return "";
        }
        return "rtsp://device/main";
    }

    std::string GetSnapshotUri(live_stream::StreamId stream_id) override {
        ++snapshot_count;
        last_snapshot_id = stream_id;
        return "http://device/snap.jpg";
    }

    bool stream_ok = true;
    live_stream::StreamId last_stream_id = live_stream::StreamId::kSnapshot;
    live_stream::StreamId last_snapshot_id = live_stream::StreamId::kSnapshot;
    int stream_count = 0;
    int snapshot_count = 0;
};

class FakeAuthService : public live_stream::IAuthService {
public:
    bool Init() override { return true; }
    bool Start() override { return true; }
    void Stop() override {}
    void Deinit() override {}

    bool SetAuditSink(live_stream::IAuthAuditSink*) override {
        return true;
    }

    live_stream::LoginResult Login(
        const live_stream::LoginRequest& request) override {
        ++login_count;
        last_user = request.user_name;
        live_stream::LoginResult result;
        if (!login_ok) {
            return result;
        }
        result.principal.user_name = request.user_name;
        result.principal.session_id = "session-1";
        result.token = "token-1";
        return result;
    }

    bool Logout(const live_stream::RequestContext&) override {
        ++logout_count;
        return true;
    }

    live_stream::TokenValidationResult ValidateToken(
        const std::string&) override {
        return live_stream::TokenValidationResult();
    }

    bool CheckPermission(const live_stream::AuthPrincipal&,
                         live_stream::AuthPermission permission,
                         const std::string&) override {
        ++check_count;
        last_permission = permission;
        return permission_status;
    }

    bool login_ok = true;
    bool permission_status = true;
    live_stream::AuthPermission last_permission =
        live_stream::AuthPermission::kReadStatus;
    std::string last_user;
    int login_count = 0;
    int check_count = 0;
    int logout_count = 0;
};

std::string SoapPost(const std::string& body,
                     const std::string& extra_headers = "") {
    return "POST /onvif/device_service HTTP/1.1\r\n" + extra_headers +
           "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" +
           body;
}

std::unique_ptr<live_stream::IOnvifService> CreateStartedService(
    FakeNetEngine* net_engine,
    FakeEventService* event_service,
    FakeSystemService* system_service,
    FakeTimeService* time_service,
    FakeUriProvider* uri_provider,
    FakeAuthService* auth_service = nullptr) {
    live_stream::OnvifServiceOptions options;
    options.enable_auth = auth_service != nullptr;
    live_stream::OnvifServiceDependencies deps;
    deps.net_engine = net_engine;
    deps.event_service = event_service;
    deps.system_service = system_service;
    deps.time_service = time_service;
    deps.uri_provider = uri_provider;
    deps.auth_service = auth_service;
    std::unique_ptr<live_stream::IOnvifService> service =
        live_stream::CreateOnvifService(options, deps);
    if (!service || !service->Init() || !service->Start()) {
        return nullptr;
    }
    return service;
}

}  // namespace

int main() {
    live_stream::OnvifServiceOptions options;
    live_stream::OnvifServiceDependencies deps;
    if (live_stream::CreateOnvifService(options, deps)->Init()) {
        return 1;
    }

    FakeNetEngine cleanup_net;
    deps.net_engine = &cleanup_net;
    cleanup_net.bind_udp_ok = false;
    std::unique_ptr<live_stream::IOnvifService> cleanup_service =
        live_stream::CreateOnvifService(options, deps);
    if (!cleanup_service->Init() || cleanup_service->Start() ||
        cleanup_net.close_tcp_count != 1) {
        return 2;
    }

    FakeNetEngine start_fail_net;
    deps.net_engine = &start_fail_net;
    start_fail_net.listen_tcp_ok = false;
    std::unique_ptr<live_stream::IOnvifService> start_fail_service =
        live_stream::CreateOnvifService(options, deps);
    if (!start_fail_service->Init() || start_fail_service->Start() ||
        start_fail_net.close_tcp_count != 0 ||
        start_fail_net.close_udp_count != 0) {
        return 3;
    }

    FakeNetEngine net_engine;
    FakeEventService event_service;
    FakeSystemService system_service;
    FakeTimeService time_service;
    FakeUriProvider uri_provider;
    time_service.status.system_time_ms = 123456;
    time_service.status.timezone = "UTC";
    std::unique_ptr<live_stream::IOnvifService> service =
        CreateStartedService(&net_engine, &event_service, &system_service,
                             &time_service, &uri_provider);
    if (!service) {
        return 4;
    }

    net_engine.DeliverTcp(SoapPost("<GetDeviceInformation/>"));
    if (net_engine.last_tcp_send.find("serial-1") == std::string::npos ||
        event_service.last_event.message != "GetDeviceInformation") {
        return 5;
    }

    net_engine.DeliverTcp(SoapPost("<GetSystemDateAndTime/>"));
    if (net_engine.last_tcp_send.find("123456") == std::string::npos) {
        return 6;
    }

    net_engine.DeliverTcp(SoapPost(
        "<SetSystemDateAndTime><tt:UnixTimeMs>2222</tt:UnixTimeMs>"
        "</SetSystemDateAndTime>"));
    if (time_service.last_set_time_ms != 2222 ||
        time_service.last_source != live_stream::TimeSyncSource::kOnvif) {
        return 7;
    }

    net_engine.DeliverTcp(SoapPost(
        "<GetStreamUri><ProfileToken>profile_sub</ProfileToken>"
        "</GetStreamUri>"));
    if (net_engine.last_tcp_send.find("rtsp://device/main") ==
            std::string::npos ||
        uri_provider.last_stream_id != live_stream::StreamId::kSub ||
        service->GetStats().stream_uri_requests != 1) {
        return 8;
    }

    net_engine.DeliverTcp(SoapPost(
        "<GetSnapshotUri><ProfileToken>bad</ProfileToken></GetSnapshotUri>"));
    if (net_engine.last_tcp_send.find("400 Bad Request") == std::string::npos ||
        service->GetStats().parse_failures == 0) {
        return 9;
    }

    net_engine.DeliverUdp("<Probe/>");
    if (net_engine.last_udp_send.find("ProbeMatches") == std::string::npos ||
        service->GetStats().discovery_requests != 1) {
        return 10;
    }
    net_engine.DeliverUdp("<Hello/>");
    if (service->GetStats().parse_failures < 2) {
        return 11;
    }

    service->Stop();
    if (net_engine.close_tcp_count != 1 || net_engine.close_udp_count != 1 ||
        !service->Start()) {
        return 12;
    }
    service->Deinit();

    FakeNetEngine missing_time_net;
    FakeEventService missing_time_event;
    std::unique_ptr<live_stream::IOnvifService> missing_time_service =
        CreateStartedService(&missing_time_net, &missing_time_event,
                             &system_service, nullptr, &uri_provider);
    if (!missing_time_service) {
        return 13;
    }
    missing_time_net.DeliverTcp(SoapPost(
        "<SetSystemDateAndTime><tt:UnixTimeMs>2222</tt:UnixTimeMs>"
        "</SetSystemDateAndTime>"));
    if (missing_time_net.last_tcp_send.find("500 Internal Server Status") ==
        std::string::npos) {
        return 14;
    }

    FakeNetEngine auth_net;
    FakeEventService auth_event;
    FakeAuthService auth_service;
    std::unique_ptr<live_stream::IOnvifService> auth_onvif =
        CreateStartedService(&auth_net, &auth_event, &system_service,
                             &time_service, &uri_provider, &auth_service);
    if (!auth_onvif) {
        return 15;
    }
    auth_net.DeliverTcp(SoapPost("<GetStreamUri/>"));
    if (auth_net.last_tcp_send.find("401 Unauthorized") == std::string::npos ||
        auth_onvif->GetStats().auth_failures != 1) {
        return 16;
    }
    auth_net.DeliverTcp(SoapPost("<GetStreamUri/>",
                                 "Authorization: Basic YWRtaW46cHc=\r\n"));
    if (auth_service.login_count != 1 || auth_service.check_count != 1 ||
        auth_service.logout_count != 1 ||
        auth_service.last_permission !=
            live_stream::AuthPermission::kPreviewVideo ||
        auth_service.last_user != "admin") {
        return 17;
    }
    auth_service.permission_status = false;
    auth_net.DeliverTcp(SoapPost("<SetSystemDateAndTime/>",
                                 "Authorization: Basic YWRtaW46cHc=\r\n"));
    if (auth_net.last_tcp_send.find("401 Unauthorized") == std::string::npos ||
        auth_onvif->GetStats().auth_failures != 2) {
        return 18;
    }
    auth_onvif->Stop();
    auth_onvif->Deinit();
    return 0;
}
