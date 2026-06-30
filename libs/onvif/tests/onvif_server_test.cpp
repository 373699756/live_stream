#include "onvif_server.h"

#include "auth.h"
#include "event.h"
#include "device.h"
#include "net.h"
#include "system.h"
#include "system/time.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace {

class FakeNetIo : public live_stream::INetIo {
public:
    class FakeLoop : public live_stream::event::Loop {
    public:
        live_stream::event::EventStatus Post(
            live_stream::event::Task task) override {
            if (task) {
                task();
            }
            return live_stream::event::EventStatus::kOk;
        }

        live_stream::event::EventStatus RunAfter(
            uint32_t,
            live_stream::event::Task,
            live_stream::event::TimerId* timer_id) override {
            if (timer_id == nullptr) {
                return live_stream::event::EventStatus::kInvalid;
            }
            *timer_id = 1;
            return live_stream::event::EventStatus::kOk;
        }

        live_stream::event::EventStatus RunEvery(
            uint32_t,
            live_stream::event::Task,
            live_stream::event::TimerId* timer_id) override {
            if (timer_id == nullptr) {
                return live_stream::event::EventStatus::kInvalid;
            }
            *timer_id = 1;
            return live_stream::event::EventStatus::kOk;
        }

        bool CancelTimer(live_stream::event::TimerId) override { return true; }
        bool IsCurrentThread() const override { return true; }
    };

    bool Start() override { return true; }
    void Stop() override {}

    live_stream::event::Loop* DefaultLoop() override {
        return &loop_;
    }

    live_stream::event::Loop* PickLoop() override {
        return &loop_;
    }

    live_stream::TcpServerId ListenTcp(
        live_stream::event::Loop*,
        const live_stream::TcpListenOptions&,
        const live_stream::TcpCallbacks& callbacks) override {
        tcp_callbacks = callbacks;
        ++listen_tcp_count;
        return listen_tcp_ok ? 10 : 0;
    }

    bool CloseTcp(live_stream::TcpServerId) override {
        ++close_tcp_count;
        return true;
    }

    live_stream::UdpSocketId BindUdp(
        live_stream::event::Loop*,
        const live_stream::UdpBindOptions&,
        const live_stream::UdpCallbacks& callbacks) override {
        udp_callbacks = callbacks;
        ++bind_udp_count;
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

    bool Close(live_stream::ConnectionId) override { return true; }

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

    bool SetUdpPeer(live_stream::UdpSocketId,
                    live_stream::NetAddress peer) override {
        last_udp_peer = peer;
        return true;
    }

    bool SendToPeer(live_stream::UdpSocketId,
                    const uint8_t* data,
                    size_t size) override {
        last_udp_send.assign(reinterpret_cast<const char*>(data), size);
        ++send_to_count;
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

    live_stream::NetAddress UdpPeerAddress(
        live_stream::UdpSocketId) const override {
        return last_udp_peer;
    }

    uint32_t PendingBytes(live_stream::ConnectionId) const override { return 0; }

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

private:
    FakeLoop loop_;
};

class FakeEvent : public live_stream::event::Dispatcher {
public:
    FakeEvent()
        : subscription_(Subscribe(
              live_stream::event::EventType::kOnvifRequestReceived,
              [this](const live_stream::event::Event& event) {
                  ++publish_count;
                  last_event = event;
              })) {}

    int publish_count = 0;
    live_stream::event::Event last_event;

private:
    live_stream::event::Subscription subscription_;
};

class FakeSystem : public live_stream::ISystem {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    live_stream::DeviceInfo GetDeviceInfo() override { return info; }

    live_stream::SystemInfo GetSystemInfo() override {
        return live_stream::SystemInfo();
    }

    live_stream::SystemCapabilities GetCapabilities() override {
        return live_stream::SystemCapabilities();
    }

    bool Reboot(const live_stream::RequestContext&) override { return true; }

    bool FactoryReset(const live_stream::RequestContext&) override {
        return true;
    }

    bool ReportHeartbeat(const std::string&) override { return true; }

    live_stream::DeviceInfo info{"model-x", "serial-1", "fw-1", "0.1.0"};
};

class FakeTime : public live_stream::ITime {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    live_stream::TimeInfo GetTimeInfo() override { return status; }

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

    bool UpdateTimeConfig(const live_stream::RequestContext&,
                          const live_stream::TimeConfig&) override {
        return true;
    }

    bool UpdateNtpConfig(const live_stream::RequestContext&,
                         const live_stream::NtpConfig&) override {
        return true;
    }

    bool UpdateBrowserSyncConfig(const live_stream::RequestContext&,
                                 bool,
                                 bool) override {
        return true;
    }

    live_stream::TimeInfo status;
    live_stream::RequestContext last_context;
    live_stream::TimeSyncSource last_source = live_stream::TimeSyncSource::kManual;
    bool set_status = true;
    int64_t last_set_time_ms = 0;
    int set_count = 0;
};

class FakeDeviceMedia : public live_stream::DeviceMedia {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }
    bool IsRestarting() const override { return false; }
    bool IsStreamStarted(live_stream::StreamId stream_id) const override {
        return stream_id == live_stream::StreamId::kMain ||
               stream_id == live_stream::StreamId::kSub;
    }
    live_stream::Codec GetStreamCodec(
        live_stream::StreamId) const override {
        return live_stream::Codec::kH264;
    }
    bool SetFrameSink(live_stream::FrameSink*) override { return true; }
    bool RequestKeyframe(live_stream::StreamId,
                         live_stream::KeyframeRequestSource) override {
        return true;
    }
    live_stream::MediaCapabilities GetCapabilities() const override {
        return live_stream::MediaCapabilities();
    }
    live_stream::MediaChannels GetChannels() const override {
        live_stream::MediaChannels channels;
        channels.vpss = live_stream::MppChannel{live_stream::MppModule::kVpss, 0, 0};
        channels.venc = live_stream::MppChannel{live_stream::MppModule::kVenc, 0, 0};
        channels.video_pipe = 0;
        channels.snap_pipe = 2;
        channels.main_size = live_stream::VideoSize{1920, 1080};
        channels.sub_vpss =
            live_stream::MppChannel{live_stream::MppModule::kVpss, 0, 1};
        channels.sub_venc =
            live_stream::MppChannel{live_stream::MppModule::kVenc, 0, 1};
        channels.sub_size = live_stream::VideoSize{640, 360};
        return channels;
    }
    live_stream::ImageInfo GetImageInfo() const override {
        return live_stream::ImageInfo();
    }
    live_stream::SnapshotFrame CaptureSnapshot(
        const live_stream::SnapshotRequest&) override {
        return live_stream::SnapshotFrame();
    }
    live_stream::SnapshotInfo GetSnapshotInfo() const override {
        return live_stream::SnapshotInfo();
    }
    live_stream::OverlayInfo GetOverlayInfo() const override {
        return live_stream::OverlayInfo();
    }
};

class FakeAuth : public live_stream::IAuth {
public:
    bool Start() override { return true; }
    void Stop() override {}
    bool IsStarted() const override { return true; }

    bool SetAuditSink(live_stream::IAuthAuditSink*) override { return true; }

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

    bool ChangePassword(const live_stream::ChangePasswordRequest&) override {
        return true;
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

std::unique_ptr<live_stream::OnvifServer> CreateStarted(
    FakeNetIo* net_io,
    FakeEvent* event,
    FakeSystem* system,
    FakeTime* time,
    FakeDeviceMedia* device,
    FakeAuth* auth = nullptr) {
    live_stream::OnvifServerOptions options;
    options.enable_auth = auth != nullptr;
    live_stream::OnvifServerDependencies deps;
    deps.net_io = net_io;
    deps.net_loop =
        net_io == nullptr ? nullptr : net_io->DefaultLoop();
    deps.event = event;
    deps.system = system;
    deps.time = time;
    deps.device = device;
    deps.auth = auth;
    std::unique_ptr<live_stream::OnvifServer> service =
        live_stream::CreateOnvifServer(options, deps);
    if (!service || !service->Start() || !service->IsStarted()) {
        return nullptr;
    }
    return service;
}

}  // namespace

int main() {
    if (std::string(live_stream::OnvifServer::Name()) != "onvif") {
        return 1;
    }

    live_stream::OnvifServerOptions options;
    live_stream::OnvifServerDependencies deps;
    if (live_stream::CreateOnvifServer(options, deps)->Start()) {
        return 2;
    }

    FakeNetIo cleanup_net;
    deps.net_io = &cleanup_net;
    deps.net_loop = cleanup_net.DefaultLoop();
    cleanup_net.bind_udp_ok = false;
    std::unique_ptr<live_stream::OnvifServer> cleanup_server =
        live_stream::CreateOnvifServer(options, deps);
    if (cleanup_server->Start() || cleanup_net.close_tcp_count != 1) {
        return 3;
    }

    FakeNetIo start_fail_net;
    deps.net_io = &start_fail_net;
    deps.net_loop = start_fail_net.DefaultLoop();
    start_fail_net.listen_tcp_ok = false;
    std::unique_ptr<live_stream::OnvifServer> start_fail_server =
        live_stream::CreateOnvifServer(options, deps);
    if (start_fail_server->Start() || start_fail_net.close_tcp_count != 0 ||
        start_fail_net.close_udp_count != 0) {
        return 4;
    }

    FakeNetIo net_io;
    FakeEvent event;
    FakeSystem system;
    FakeTime time;
    FakeDeviceMedia device;
    time.status.system_time_ms = 123456;
    time.status.timezone = "UTC";

    std::unique_ptr<live_stream::OnvifServer> service =
        CreateStarted(&net_io, &event, &system,
                      &time, &device);
    if (!service) {
        return 5;
    }

    net_io.DeliverTcp(SoapPost("<GetDeviceInformation/>"));
    if (net_io.last_tcp_send.find("serial-1") == std::string::npos ||
        event.last_event.message != "GetDeviceInformation") {
        return 6;
    }

    net_io.DeliverTcp(SoapPost("<GetSystemDateAndTime/>"));
    if (net_io.last_tcp_send.find("123456") == std::string::npos) {
        return 7;
    }

    net_io.DeliverTcp(SoapPost(
        "<SetSystemDateAndTime><tt:UnixTimeMs>2222</tt:UnixTimeMs>"
        "</SetSystemDateAndTime>"));
    if (time.last_set_time_ms != 2222 ||
        time.last_source != live_stream::TimeSyncSource::kOnvif) {
        return 8;
    }

    net_io.DeliverTcp(SoapPost(
        "<GetStreamUri><ProfileToken>profile_sub</ProfileToken>"
        "</GetStreamUri>"));
    if (net_io.last_tcp_send.find("rtsp://") == std::string::npos ||
        net_io.last_tcp_send.find("profile_sub") == std::string::npos ||
        service->GetStats().stream_uri_requests != 1) {
        return 9;
    }

    net_io.DeliverTcp(SoapPost(
        "<GetSnapshotUri><ProfileToken>bad</ProfileToken></GetSnapshotUri>"));
    if (net_io.last_tcp_send.find("400 Bad Request") == std::string::npos ||
        service->GetStats().parse_failures == 0) {
        return 10;
    }

    net_io.DeliverUdp("<Probe/>");
    if (net_io.last_udp_send.find("ProbeMatches") == std::string::npos ||
        net_io.last_udp_send.find("EndpointReference") ==
            std::string::npos ||
        net_io.last_udp_send.find("NetworkVideoTransmitter") ==
            std::string::npos ||
        net_io.last_udp_send.find("XAddrs") == std::string::npos ||
        service->GetStats().discovery_requests != 1) {
        return 11;
    }

    net_io.DeliverUdp("<Hello/>");
    if (service->GetStats().parse_failures < 2) {
        return 12;
    }

    service->Stop();
    if (net_io.close_tcp_count != 1 || net_io.close_udp_count != 1 ||
        service->IsStarted()) {
        return 13;
    }

    FakeNetIo missing_time_net;
    FakeEvent missing_time_event;
    std::unique_ptr<live_stream::OnvifServer> missing_time_server =
        CreateStarted(&missing_time_net, &missing_time_event,
                      &system, nullptr, &device);
    if (!missing_time_server) {
        return 14;
    }
    missing_time_net.DeliverTcp(SoapPost(
        "<SetSystemDateAndTime><tt:UnixTimeMs>2222</tt:UnixTimeMs>"
        "</SetSystemDateAndTime>"));
    if (missing_time_net.last_tcp_send.find("500 Internal Server Status") ==
        std::string::npos) {
        return 15;
    }

    FakeNetIo auth_net;
    FakeEvent auth_event;
    FakeAuth auth;
    std::unique_ptr<live_stream::OnvifServer> auth_onvif =
        CreateStarted(&auth_net, &auth_event, &system,
                      &time, &device, &auth);
    if (!auth_onvif) {
        return 16;
    }
    auth_net.DeliverTcp(SoapPost("<GetStreamUri/>"));
    if (auth_net.last_tcp_send.find("401 Unauthorized") == std::string::npos ||
        auth_onvif->GetStats().auth_failures != 1) {
        return 17;
    }
    auth_net.DeliverTcp(SoapPost("<GetStreamUri/>",
                                 "Authorization: Basic YWRtaW46cHc=\r\n"));
    if (auth.login_count != 1 || auth.check_count != 1 ||
        auth.logout_count != 1 ||
        auth.last_permission !=
            live_stream::AuthPermission::kPreviewVideo ||
        auth.last_user != "admin") {
        return 18;
    }
    auth.permission_status = false;
    auth_net.DeliverTcp(SoapPost("<SetSystemDateAndTime/>",
                                 "Authorization: Basic YWRtaW46cHc=\r\n"));
    if (auth_net.last_tcp_send.find("401 Unauthorized") == std::string::npos ||
        auth_onvif->GetStats().auth_failures != 2) {
        return 19;
    }

    auth_onvif->Stop();
    return auth_onvif->IsStarted() ? 20 : 0;
}
