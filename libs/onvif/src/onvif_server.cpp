#include "onvif_server.h"

#include "event.h"
#include "socket_io.h"
#include "onvif_auth.h"
#include "onvif_device.h"
#include "onvif_discovery.h"
#include "onvif_http.h"
#include "onvif_media.h"
#include "onvif_soap.h"
#include "onvif_types.h"
#include "runtime.h"
#include "service_registry.h"

#include <mutex>
#include <string>

namespace live_stream {
namespace {

constexpr const char *kOnvifServerName = "onvif";

struct OnvifRequestSnapshot {
    OnvifServerOptions options;
    UdpSocketId discovery_socket_id = 0;
    bool started = false;
};

}  // namespace

class OnvifServer::Impl {
public:
    Impl(const OnvifServerOptions &options,
         event::Loop *socket_loop,
         ISystem *system,
         ITime *time,
         DeviceMedia *device)
        : options_(options),
          socket_io_(Runtime::SocketIo()),
          net_loop_(socket_loop),
          auth_(Runtime::Auth()),
          event_(Runtime::EventCenter()),
          system_(system),
          time_(time),
          device_(device),
          rtsp_(ServiceRegistry::Rtsp()) {}

    ~Impl() {
        Stop();
    }

    bool Start() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!PrepareLocked()) {
            return false;
        }
        if (started_) {
            return true;
        }

        // ONVIF 有两个入口：TCP device service 处理 SOAP，UDP discovery 只处理
        // Probe。二者共享 socket_io，但协议状态不跨入口保存。
        TcpListenOptions tcp_options;
        tcp_options.address.ip = options_.listen_ip;
        tcp_options.address.port = options_.device_service_port;
        tcp_options.max_connections = 16;
        TcpCallbacks tcp_callbacks;
        tcp_callbacks.user = this;
        tcp_callbacks.on_read = &OnvifServer::Impl::HandleTcpRead;
        const TcpServerId tcp_server_id =
            socket_io_->ListenTcp(net_loop_, tcp_options, tcp_callbacks);
        if (tcp_server_id == 0) {
            return false;
        }
        tcp_server_id_ = tcp_server_id;

        if (options_.discovery_enabled) {
            UdpBindOptions udp_options;
            udp_options.address.ip = options_.listen_ip;
            udp_options.address.port = options_.discovery_port;
            UdpCallbacks udp_callbacks;
            udp_callbacks.user = this;
            udp_callbacks.on_read = &OnvifServer::Impl::HandleUdpRead;
            const UdpSocketId udp_socket_id =
                socket_io_->BindUdp(net_loop_, udp_options,
                                 udp_callbacks);
            if (udp_socket_id == 0) {
                UdpSocketId close_udp_socket_id = 0;
                TcpServerId close_tcp_server_id = 0;
                TakeSocketsLocked(&close_udp_socket_id,
                                  &close_tcp_server_id);
                lock.unlock();
                CloseSockets(close_udp_socket_id, close_tcp_server_id);
                return false;
            }
            udp_socket_id_ = udp_socket_id;
        }

        started_ = true;
        return true;
    }

    void Stop() {
        UdpSocketId udp_socket_id = 0;
        TcpServerId tcp_server_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            TakeSocketsLocked(&udp_socket_id, &tcp_server_id);
            started_ = false;
            prepared_ = false;
        }
        CloseSockets(udp_socket_id, tcp_server_id);
    }

    bool ApplyOptions(const OnvifServerOptions &options) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!CanApplyOptionsLocked(options)) {
            return false;
        }
        // 只允许更新不会重建 socket/parser 边界的字段。端口、service_path、
        // discovery 开关和消息上限必须重启后生效。
        options_.advertise_ip = options.advertise_ip;
        options_.enable_auth = options.enable_auth;
        options_.manufacturer = options.manufacturer;
        options_.model = options.model;
        options_.firmware_version = options.firmware_version;
        options_.http_port = options.http_port;
        return true;
    }

    bool IsStarted() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return prepared_ && started_;
    }

    OnvifServerStats GetStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

private:
    bool PrepareLocked() {
        if (prepared_) {
            return true;
        }
        if (socket_io_ == nullptr ||
            net_loop_ == nullptr ||
            options_.device_service_port == 0 ||
            options_.discovery_port == 0 ||
            options_.max_request_bytes == 0 ||
            options_.max_request_bytes > onvif::kMaxOnvifHttpMessageBytes ||
            options_.service_path.empty()) {
            return false;
        }
        if (options_.enable_auth && auth_ == nullptr) {
            return false;
        }
        prepared_ = true;
        return true;
    }

    bool CanApplyOptionsLocked(const OnvifServerOptions &options) const {
        // listener 和 parser 相关字段一旦启动就固定，避免配置落盘成功但当前
        // ONVIF socket 仍运行在旧边界。
        return options.listen_ip == options_.listen_ip &&
               options.endpoint_uuid == options_.endpoint_uuid &&
               options.device_service_port ==
                   options_.device_service_port &&
               options.discovery_port == options_.discovery_port &&
               options.discovery_enabled == options_.discovery_enabled &&
               options.service_path == options_.service_path &&
               options.max_request_bytes == options_.max_request_bytes;
    }

    static void HandleTcpRead(void *user,
                              ConnectionId connection_id,
                              const uint8_t *data,
                              size_t size) {
        OnvifServer::Impl *self = static_cast<OnvifServer::Impl *>(user);
        if (self != nullptr) {
            self->HandleTcpMessage(connection_id, data,
                                   static_cast<uint32_t>(size));
        }
    }

    static void HandleUdpRead(void *user,
                              UdpSocketId,
                              SocketAddress address,
                              const uint8_t *data,
                              size_t size) {
        OnvifServer::Impl *self = static_cast<OnvifServer::Impl *>(user);
        if (self != nullptr) {
            self->HandleUdpMessage(address, data, static_cast<uint32_t>(size));
        }
    }

    void HandleUdpMessage(const SocketAddress &address,
                          const uint8_t *data,
                          uint32_t size) {
        const OnvifRequestSnapshot snapshot = SnapshotForRequest();
        if (!snapshot.started || data == nullptr || size == 0 ||
            size > snapshot.options.max_request_bytes) {
            IncrementParseFailures();
            return;
        }
        // Discovery 是无连接 UDP，请求体不能超过 ONVIF HTTP 消息上限；
        // 回复直接发回 Probe 来源地址，不使用 selected peer。
        const std::string request(reinterpret_cast<const char *>(data), size);
        if (!onvif::IsOnvifProbeRequest(request)) {
            IncrementParseFailures();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.discovery_requests;
        }
        const std::string response = onvif::BuildDiscoveryProbeMatches(
            snapshot.options, system_, AdvertiseIp(snapshot.options),
            request);
        if (snapshot.discovery_socket_id != 0) {
            static_cast<void>(socket_io_->SendTo(
                snapshot.discovery_socket_id, address,
                reinterpret_cast<const uint8_t *>(response.data()),
                response.size()));
        }
    }

    void HandleTcpMessage(ConnectionId connection_id,
                          const uint8_t *data,
                          uint32_t size) {
        const OnvifRequestSnapshot snapshot = SnapshotForRequest();
        if (!snapshot.started || data == nullptr || size == 0 ||
            size > snapshot.options.max_request_bytes) {
            IncrementParseFailures();
            return;
        }
        // 当前 device service 一次 TCP read 处理一个完整 SOAP HTTP 请求，
        // 响应后 CloseAfterSend；不做 HTTP keep-alive/pipeline。
        const std::string request(reinterpret_cast<const char *>(data), size);
        const onvif::OnvifHttpRequest parsed =
            onvif::ParseOnvifHttpRequest(request);
        onvif::OnvifAction action = onvif::OnvifAction::kUnknown;
        std::string body;
        std::string extra_headers;
        uint32_t status_code = 200;
        std::string reason = "OK";

        if (parsed.method != "POST" ||
            parsed.path != snapshot.options.service_path) {
            IncrementParseFailures();
            status_code = 400;
            reason = "Bad Request";
            body = onvif::BuildSoapFaultEnvelope("invalid onvif http request");
        } else {
            action = onvif::ParseSoapAction(parsed.body);
            if (action == onvif::OnvifAction::kUnknown) {
                // 未识别 action 仍返回 SOAP fault，保持 NVR 互通；统计为 parse failure。
                IncrementParseFailures();
                status_code = 400;
                reason = "Bad Request";
                body = onvif::BuildSoapFaultEnvelope(
                    "unsupported onvif action");
            } else if (!onvif::AuthorizeOnvifAction(
                           auth_, snapshot.options.enable_auth,
                           parsed.headers, action)) {
                IncrementAuthFailures();
                status_code = 401;
                reason = "Unauthorized";
                extra_headers = "WWW-Authenticate: Basic realm=\"onvif\"\r\n";
                body = onvif::BuildSoapFaultEnvelope("unauthorized");
            } else {
                body = HandleSoapAction(action, parsed.body, snapshot.options,
                                        &status_code, &reason);
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.soap_requests;
        }
        PublishRequestEvent(action);

        // 无论成功还是 SOAP fault，都用 HTTP 响应承载，并在发送完成后关闭连接。
        const std::string response =
            onvif::BuildOnvifHttpResponse(status_code, reason, body,
                                          extra_headers);
        static_cast<void>(socket_io_->Send(
            connection_id, reinterpret_cast<const uint8_t *>(response.data()),
            response.size()));
        static_cast<void>(
            socket_io_->CloseAfterSend(connection_id));
    }

    std::string HandleSoapAction(onvif::OnvifAction action,
                                 const std::string &request,
                                 const OnvifServerOptions &options,
                                 uint32_t *status_code,
                                 std::string *reason) {
        // action 分发只做协议 DTO 转换；设备信息、时间、媒体 URL 分别从拥有模块读取。
        switch (action) {
            case onvif::OnvifAction::kGetDeviceInformation:
                return onvif::BuildSoapEnvelope(
                    onvif::BuildDeviceInformationBody(
                        options, system_));
            case onvif::OnvifAction::kGetSystemDateAndTime:
                return onvif::BuildSoapEnvelope(
                    onvif::BuildSystemDateAndTimeBody(
                        time_));
            case onvif::OnvifAction::kSetSystemDateAndTime:
                return onvif::BuildSoapEnvelope(
                    onvif::BuildSetSystemDateAndTimeBody(
                        time_, request, status_code, reason));
            case onvif::OnvifAction::kGetProfiles: {
                const onvif::OnvifMediaUris media_uris =
                    BuildMediaUrisForRequest(options);
                return onvif::BuildSoapEnvelope(
                    onvif::BuildProfilesBody(media_uris));
            }
            case onvif::OnvifAction::kGetStreamUri: {
                StreamId stream_id = StreamId::kMain;
                if (!onvif::ParseProfileToken(request, &stream_id)) {
                    IncrementParseFailures();
                    return onvif::BuildSoapEnvelope(
                        onvif::BuildProfileFaultBody(status_code, reason));
                }
                const onvif::OnvifMediaUris media_uris =
                    BuildMediaUrisForRequest(options);
                const onvif::OnvifBody result = onvif::BuildStreamUriBody(
                    media_uris, stream_id, status_code, reason);
                if (result.success) {
                    IncrementStreamUriRequests();
                }
                return onvif::BuildSoapEnvelope(result.body);
            }
            case onvif::OnvifAction::kGetSnapshotUri: {
                StreamId stream_id = StreamId::kMain;
                if (!onvif::ParseProfileToken(request, &stream_id)) {
                    IncrementParseFailures();
                    return onvif::BuildSoapEnvelope(
                        onvif::BuildProfileFaultBody(status_code, reason));
                }
                const onvif::OnvifMediaUris media_uris =
                    BuildMediaUrisForRequest(options);
                const onvif::OnvifBody result = onvif::BuildSnapshotUriBody(
                    media_uris, stream_id, status_code, reason);
                if (result.success) {
                    IncrementSnapshotUriRequests();
                }
                return onvif::BuildSoapEnvelope(result.body);
            }
            case onvif::OnvifAction::kUnknown:
                break;
        }
        if (status_code != nullptr) {
            *status_code = 400;
        }
        if (reason != nullptr) {
            *reason = "Bad Request";
        }
        return onvif::BuildSoapFaultEnvelope("unsupported onvif action");
    }

    onvif::OnvifMediaUris BuildMediaUrisForRequest(
        const OnvifServerOptions &options) const {
        return onvif::BuildOnvifMediaUris(options, device_, rtsp_,
                                          AdvertiseIp(options));
    }

    std::string AdvertiseIp(const OnvifServerOptions &options) const {
        if (!options.advertise_ip.empty()) {
            return options.advertise_ip;
        }
        // listen_ip 为 0.0.0.0 时不能作为 XAddr 主机名，只能回退到本地默认值；
        // 正式部署应由 app 配置 advertise_ip。
        if (!options.listen_ip.empty() && options.listen_ip != "0.0.0.0") {
            return options.listen_ip;
        }
        return "127.0.0.1";
    }

    void PublishRequestEvent(onvif::OnvifAction action) {
        if (event_ == nullptr) {
            return;
        }
        event::Event request_event;
        request_event.type = event::EventType::kOnvifRequestReceived;
        request_event.source = kOnvifServerName;
        request_event.msg = onvif::ActionName(action);
        static_cast<void>(event_->Publish(request_event));
    }

    void IncrementParseFailures() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.parse_failures;
    }

    void IncrementAuthFailures() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.auth_failures;
    }

    void IncrementStreamUriRequests() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.stream_uri_requests;
    }

    void IncrementSnapshotUriRequests() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.snapshot_uri_requests;
    }

    OnvifRequestSnapshot SnapshotForRequest() const {
        std::lock_guard<std::mutex> lock(mutex_);
        OnvifRequestSnapshot snapshot;
        snapshot.options = options_;
        snapshot.discovery_socket_id = udp_socket_id_;
        snapshot.started = started_;
        return snapshot;
    }

    void CloseSockets(UdpSocketId udp_socket_id,
                      TcpServerId tcp_server_id) {
        if (udp_socket_id != 0) {
            static_cast<void>(socket_io_->CloseUdp(udp_socket_id));
        }
        if (tcp_server_id != 0) {
            static_cast<void>(socket_io_->CloseTcp(tcp_server_id));
        }
    }

    void TakeSocketsLocked(UdpSocketId *udp_socket_id,
                           TcpServerId *tcp_server_id) {
        if (udp_socket_id != nullptr) {
            *udp_socket_id = udp_socket_id_;
        }
        if (tcp_server_id != nullptr) {
            *tcp_server_id = tcp_server_id_;
        }
        udp_socket_id_ = 0;
        tcp_server_id_ = 0;
    }

    OnvifServerOptions options_;
    ISocketIo *socket_io_ = nullptr;
    event::Loop *net_loop_ = nullptr;
    IAuth *auth_ = nullptr;
    event::EventCenter *event_ = nullptr;
    ISystem *system_ = nullptr;
    ITime *time_ = nullptr;
    DeviceMedia *device_ = nullptr;
    IRtspSessionReader *rtsp_ = nullptr;
    TcpServerId tcp_server_id_ = 0;
    UdpSocketId udp_socket_id_ = 0;
    mutable std::mutex mutex_;
    OnvifServerStats stats_;
    bool prepared_ = false;
    bool started_ = false;
};

OnvifServer::OnvifServer(const OnvifServerOptions &options,
                         event::Loop *socket_loop,
                         ISystem *system,
                         ITime *time,
                         DeviceMedia *device)
    : impl_(new Impl(options, socket_loop, system, time, device)) {}

OnvifServer::~OnvifServer() = default;

bool OnvifServer::Start() {
    return impl_->Start();
}

void OnvifServer::Stop() {
    impl_->Stop();
}

bool OnvifServer::ApplyOptions(const OnvifServerOptions &options) {
    return impl_->ApplyOptions(options);
}

bool OnvifServer::IsStarted() const {
    return impl_->IsStarted();
}

OnvifServerStats OnvifServer::GetStats() const {
    return impl_->GetStats();
}

const char *OnvifServer::Name() {
    return kOnvifServerName;
}

std::unique_ptr<OnvifServer> CreateOnvifServer(
    const OnvifServerOptions &options,
    event::Loop *socket_loop,
    ISystem *system,
    ITime *time,
    DeviceMedia *device) {
    return std::unique_ptr<OnvifServer>(
        new OnvifServer(options, socket_loop, system, time, device));
}

}  // namespace live_stream
