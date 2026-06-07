#include "onvif_server.h"

#include "event.h"
#include "net.h"
#include "onvif_auth.h"
#include "onvif_device.h"
#include "onvif_discovery.h"
#include "onvif_http.h"
#include "onvif_media.h"
#include "onvif_soap.h"
#include "onvif_types.h"

#include <mutex>
#include <string>

namespace live_stream {
namespace {

constexpr const char *kOnvifServerName = "onvif";

}  // namespace

class OnvifServer::Impl {
public:
    Impl(const OnvifServerOptions &options,
         const OnvifServerDependencies &dependencies)
        : options_(options), dependencies_(dependencies) {}

    ~Impl() {
        Stop();
    }

    bool Start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!PrepareLocked()) {
            return false;
        }
        if (started_) {
            return true;
        }

        TcpListenOptions tcp_options;
        tcp_options.address.ip = options_.listen_ip;
        tcp_options.address.port = options_.device_service_port;
        tcp_options.max_connections = 16;
        TcpCallbacks tcp_callbacks;
        tcp_callbacks.user = this;
        tcp_callbacks.on_read = &OnvifServer::Impl::HandleTcpRead;
        const TcpServerId tcp_server_id =
            dependencies_.net_engine->ListenTcp(tcp_options, tcp_callbacks);
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
                dependencies_.net_engine->BindUdp(udp_options, udp_callbacks);
            if (udp_socket_id == 0) {
                CleanupSocketsLocked();
                return false;
            }
            udp_socket_id_ = udp_socket_id;
        }

        started_ = true;
        return true;
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        CleanupSocketsLocked();
        started_ = false;
        prepared_ = false;
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
        if (dependencies_.net_engine == nullptr ||
            options_.device_service_port == 0 ||
            options_.discovery_port == 0 ||
            options_.max_request_bytes == 0 ||
            options_.max_request_bytes > onvif::kMaxOnvifHttpMessageBytes ||
            options_.service_path.empty()) {
            return false;
        }
        if (options_.enable_auth && dependencies_.auth == nullptr) {
            return false;
        }
        prepared_ = true;
        return true;
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
                              NetAddress address,
                              const uint8_t *data,
                              size_t size) {
        OnvifServer::Impl *self = static_cast<OnvifServer::Impl *>(user);
        if (self != nullptr) {
            self->HandleUdpMessage(address, data, static_cast<uint32_t>(size));
        }
    }

    void HandleUdpMessage(const NetAddress &address,
                          const uint8_t *data,
                          uint32_t size) {
        if (data == nullptr || size == 0 || size > options_.max_request_bytes) {
            IncrementParseFailures();
            return;
        }
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
            options_, dependencies_.system, AdvertiseIp(), request);
        if (udp_socket_id_ != 0 && dependencies_.net_engine != nullptr) {
            static_cast<void>(dependencies_.net_engine->SendTo(
                udp_socket_id_, address,
                reinterpret_cast<const uint8_t *>(response.data()),
                response.size()));
        }
    }

    void HandleTcpMessage(ConnectionId connection_id,
                          const uint8_t *data,
                          uint32_t size) {
        if (data == nullptr || size == 0 || size > options_.max_request_bytes) {
            IncrementParseFailures();
            return;
        }
        const std::string request(reinterpret_cast<const char *>(data), size);
        const onvif::OnvifHttpRequest parsed =
            onvif::ParseOnvifHttpRequest(request);
        onvif::OnvifAction action = onvif::OnvifAction::kUnknown;
        std::string body;
        std::string extra_headers;
        uint32_t status = 200;
        std::string reason = "OK";

        if (parsed.method != "POST" || parsed.path != options_.service_path) {
            IncrementParseFailures();
            status = 400;
            reason = "Bad Request";
            body = onvif::BuildSoapFaultEnvelope("invalid onvif http request");
        } else {
            action = onvif::ParseSoapAction(parsed.body);
            if (action == onvif::OnvifAction::kUnknown) {
                IncrementParseFailures();
                status = 400;
                reason = "Bad Request";
                body = onvif::BuildSoapFaultEnvelope(
                    "unsupported onvif action");
            } else if (!onvif::AuthorizeOnvifAction(
                           dependencies_.auth, options_.enable_auth,
                           parsed.headers, action)) {
                IncrementAuthFailures();
                status = 401;
                reason = "Unauthorized";
                extra_headers = "WWW-Authenticate: Basic realm=\"onvif\"\r\n";
                body = onvif::BuildSoapFaultEnvelope("unauthorized");
            } else {
                body = HandleSoapAction(action, parsed.body, &status, &reason);
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.soap_requests;
        }
        PublishRequestEvent(action);

        const std::string response =
            onvif::BuildOnvifHttpResponse(status, reason, body, extra_headers);
        static_cast<void>(dependencies_.net_engine->Send(
            connection_id, reinterpret_cast<const uint8_t *>(response.data()),
            response.size()));
        static_cast<void>(
            dependencies_.net_engine->CloseAfterSend(connection_id));
    }

    std::string HandleSoapAction(onvif::OnvifAction action,
                                 const std::string &request,
                                 uint32_t *status,
                                 std::string *reason) {
        switch (action) {
            case onvif::OnvifAction::kGetDeviceInformation:
                return onvif::BuildSoapEnvelope(
                    onvif::BuildDeviceInformationBody(
                        options_, dependencies_.system));
            case onvif::OnvifAction::kGetSystemDateAndTime:
                return onvif::BuildSoapEnvelope(
                    onvif::BuildSystemDateAndTimeBody(
                        dependencies_.time));
            case onvif::OnvifAction::kSetSystemDateAndTime:
                return onvif::BuildSoapEnvelope(
                    onvif::BuildSetSystemDateAndTimeBody(
                        dependencies_.time, request, status, reason));
            case onvif::OnvifAction::kGetProfiles: {
                const onvif::OnvifMediaUris media_uris =
                    BuildMediaUrisForRequest();
                return onvif::BuildSoapEnvelope(
                    onvif::BuildProfilesBody(media_uris));
            }
            case onvif::OnvifAction::kGetStreamUri: {
                StreamId stream_id = StreamId::kMain;
                if (!onvif::ParseProfileToken(request, &stream_id)) {
                    IncrementParseFailures();
                    return onvif::BuildSoapEnvelope(
                        onvif::BuildProfileFaultBody(status, reason));
                }
                const onvif::OnvifMediaUris media_uris =
                    BuildMediaUrisForRequest();
                const onvif::OnvifBody result = onvif::BuildStreamUriBody(
                    media_uris, stream_id, status, reason);
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
                        onvif::BuildProfileFaultBody(status, reason));
                }
                const onvif::OnvifMediaUris media_uris =
                    BuildMediaUrisForRequest();
                const onvif::OnvifBody result = onvif::BuildSnapshotUriBody(
                    media_uris, stream_id, status, reason);
                if (result.success) {
                    IncrementSnapshotUriRequests();
                }
                return onvif::BuildSoapEnvelope(result.body);
            }
            case onvif::OnvifAction::kUnknown:
                break;
        }
        if (status != nullptr) {
            *status = 400;
        }
        if (reason != nullptr) {
            *reason = "Bad Request";
        }
        return onvif::BuildSoapFaultEnvelope("unsupported onvif action");
    }

    onvif::OnvifMediaUris BuildMediaUrisForRequest() const {
        return onvif::BuildOnvifMediaUris(options_, dependencies_,
                                          AdvertiseIp());
    }

    std::string AdvertiseIp() const {
        if (!options_.advertise_ip.empty()) {
            return options_.advertise_ip;
        }
        if (!options_.listen_ip.empty() && options_.listen_ip != "0.0.0.0") {
            return options_.listen_ip;
        }
        return "127.0.0.1";
    }

    void PublishRequestEvent(onvif::OnvifAction action) {
        if (dependencies_.event == nullptr) {
            return;
        }
        Event event;
        event.type = EventType::kOnvifRequestReceived;
        event.source = kOnvifServerName;
        event.message = onvif::ActionName(action);
        static_cast<void>(dependencies_.event->Publish(event));
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

    void CleanupSocketsLocked() {
        if (udp_socket_id_ != 0 && dependencies_.net_engine != nullptr) {
            static_cast<void>(dependencies_.net_engine->CloseUdp(
                udp_socket_id_));
            udp_socket_id_ = 0;
        }
        if (tcp_server_id_ != 0 && dependencies_.net_engine != nullptr) {
            static_cast<void>(dependencies_.net_engine->CloseTcp(
                tcp_server_id_));
            tcp_server_id_ = 0;
        }
    }

    OnvifServerOptions options_;
    OnvifServerDependencies dependencies_;
    TcpServerId tcp_server_id_ = 0;
    UdpSocketId udp_socket_id_ = 0;
    mutable std::mutex mutex_;
    OnvifServerStats stats_;
    bool prepared_ = false;
    bool started_ = false;
};

OnvifServer::OnvifServer(const OnvifServerOptions &options,
                         const OnvifServerDependencies &dependencies)
    : impl_(new Impl(options, dependencies)) {}

OnvifServer::~OnvifServer() = default;

bool OnvifServer::Start() {
    return impl_ != nullptr && impl_->Start();
}

void OnvifServer::Stop() {
    if (impl_ != nullptr) {
        impl_->Stop();
    }
}

bool OnvifServer::IsStarted() const {
    return impl_ != nullptr && impl_->IsStarted();
}

OnvifServerStats OnvifServer::GetStats() const {
    if (impl_ == nullptr) {
        return OnvifServerStats{};
    }
    return impl_->GetStats();
}

const char *OnvifServer::Name() {
    return kOnvifServerName;
}

std::unique_ptr<OnvifServer> CreateOnvifServer(
    const OnvifServerOptions &options,
    const OnvifServerDependencies &dependencies) {
    return std::unique_ptr<OnvifServer>(
        new OnvifServer(options, dependencies));
}

}  // namespace live_stream
