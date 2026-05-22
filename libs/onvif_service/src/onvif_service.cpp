#include "onvif_service.h"

#include "auth_service.h"
#include "event_service.h"
#include "media_service.h"
#include "net_service.h"
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

using onvif_internal::ActionName;
using onvif_internal::AuthorizeOnvifRequest;
using onvif_internal::Contains;
using onvif_internal::DeviceInformationBody;
using onvif_internal::DiscoveryProbeMatchesBody;
using onvif_internal::HttpRequest;
using onvif_internal::HttpResponse;
using onvif_internal::IOnvifUriProvider;
using onvif_internal::kMaxHttpResponseBytes;
using onvif_internal::OnvifAction;
using onvif_internal::OnvifBodyResult;
using onvif_internal::ParseAction;
using onvif_internal::ParseHttpRequest;
using onvif_internal::ParseStreamId;
using onvif_internal::ProfileFault;
using onvif_internal::ProfilesBody;
using onvif_internal::SetSystemDateAndTimeBody;
using onvif_internal::SnapshotUriBody;
using onvif_internal::SoapEnvelope;
using onvif_internal::SoapFault;
using onvif_internal::StreamUriBody;
using onvif_internal::SystemDateAndTimeBody;

class ServiceUriProvider : public IOnvifUriProvider {
public:
    ServiceUriProvider(const OnvifServiceOptions* options,
                       const OnvifServiceDependencies* dependencies)
        : options_(options), dependencies_(dependencies) {}

    std::string GetStreamUri(StreamId stream_id) override {
        if (options_ == nullptr || dependencies_ == nullptr) {
            return std::string();
        }
        if (dependencies_->media_service != nullptr &&
            !dependencies_->media_service->IsStreamStarted(stream_id)) {
            return std::string();
        }
        return std::string("rtsp://") + options_->advertise_ip + ":" +
               std::to_string(options_->rtsp_port) + StreamPath(stream_id);
    }

    std::string GetSnapshotUri(StreamId stream_id) override {
        if (options_ == nullptr) {
            return std::string();
        }
        return std::string("http://") + options_->advertise_ip + ":" +
               std::to_string(options_->http_port) + SnapshotPath(stream_id);
    }

private:
    const char* StreamPath(StreamId stream_id) const {
        return stream_id == StreamId::kSub ? "/live/sub" : "/live/main";
    }

    const std::string& SnapshotPath(StreamId stream_id) const {
        return stream_id == StreamId::kSub ? options_->snapshot_sub_path
                                           : options_->snapshot_main_path;
    }

    const OnvifServiceOptions* options_ = nullptr;
    const OnvifServiceDependencies* dependencies_ = nullptr;
};

class OnvifServiceImpl : public IOnvifService {
public:
    OnvifServiceImpl(const OnvifServiceOptions& options,
                     const OnvifServiceDependencies& dependencies)
        : options_(options),
          dependencies_(dependencies),
          uri_provider_(&options_, &dependencies_) {}

    bool Prepare() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return true;
        }
        if (dependencies_.net_engine == nullptr ||
            options_.device_service_port == 0 ||
            options_.discovery_port == 0 ||
            options_.max_request_bytes == 0 ||
            options_.max_request_bytes > kMaxHttpResponseBytes ||
            options_.service_path.empty()) {
            return false;
        }
        if (options_.enable_auth && dependencies_.auth_service == nullptr) {
            return false;
        }
        initialized_ = true;
        return true;
    }

    bool Start() override {
        if (!Prepare()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_) {
            return true;
        }

        TcpListenOptions tcp_config;
        tcp_config.address.ip = options_.listen_ip;
        tcp_config.address.port = options_.device_service_port;
        tcp_config.max_connections = 16;
        TcpCallbacks tcp_callbacks;
        tcp_callbacks.user = this;
        tcp_callbacks.on_read = &OnvifServiceImpl::HandleTcpRead;
        const TcpServerId tcp_result =
            dependencies_.net_engine->ListenTcp(tcp_config, tcp_callbacks);
        if (tcp_result == 0) {
            return false;
        }
        tcp_server_id_ = tcp_result;

        if (options_.discovery_enabled) {
            UdpBindOptions udp_config;
            udp_config.address.ip = options_.listen_ip;
            udp_config.address.port = options_.discovery_port;
            UdpCallbacks udp_callbacks;
            udp_callbacks.user = this;
            udp_callbacks.on_read = &OnvifServiceImpl::HandleUdpRead;
            const UdpSocketId udp_result =
                dependencies_.net_engine->BindUdp(udp_config, udp_callbacks);
            if (udp_result == 0) {
                CleanupSocketsLocked();
                return false;
            }
            udp_socket_id_ = udp_result;
        }

        started_ = true;
        return true;
    }

    void Stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        CleanupSocketsLocked();
        started_ = false;
    }

    bool IsStarted() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && started_;
    }

    void Release() {
        Stop();
        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = false;
    }

    OnvifServiceStats GetStats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

private:
    void HandleUdpMessage(const NetAddress& address,
                          const uint8_t* data,
                          uint32_t size) {
        if (data == nullptr || size == 0 || size > options_.max_request_bytes) {
            IncrementParseFailures();
            return;
        }
        const std::string request(reinterpret_cast<const char*>(data), size);
        if (!Contains(request, "Probe")) {
            IncrementParseFailures();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.discovery_requests;
        }
        const std::string response =
            DiscoveryProbeMatchesBody(options_, AdvertiseIp());
        if (udp_socket_id_ != 0 && dependencies_.net_engine != nullptr) {
            static_cast<void>(dependencies_.net_engine->SendTo(
                udp_socket_id_, address,
                reinterpret_cast<const uint8_t*>(response.data()),
                response.size()));
        }
    }

    static void HandleTcpRead(void* user,
                              ConnectionId id,
                              const uint8_t* data,
                              size_t size) {
        OnvifServiceImpl* self = static_cast<OnvifServiceImpl*>(user);
        if (self != nullptr) {
            self->HandleTcpMessage(id, data, static_cast<uint32_t>(size));
        }
    }

    static void HandleUdpRead(void* user,
                              UdpSocketId,
                              NetAddress address,
                              const uint8_t* data,
                              size_t size) {
        OnvifServiceImpl* self = static_cast<OnvifServiceImpl*>(user);
        if (self != nullptr) {
            self->HandleUdpMessage(address, data, static_cast<uint32_t>(size));
        }
    }

    void HandleTcpMessage(ConnectionId connection_id,
                          const uint8_t* data,
                          uint32_t size) {
        if (data == nullptr || size == 0 ||
            size > options_.max_request_bytes) {
            IncrementParseFailures();
            return;
        }
        const std::string request(reinterpret_cast<const char*>(data), size);
        HttpRequest parsed = ParseHttpRequest(request);
        OnvifAction action = OnvifAction::kUnknown;
        std::string body;
        std::string extra_headers;
        uint32_t status = 200;
        std::string reason = "OK";
        if (parsed.method != "POST" || parsed.path != options_.service_path) {
            IncrementParseFailures();
            status = 400;
            reason = "Bad Request";
            body = SoapFault("invalid onvif http request");
        } else {
            action = ParseAction(parsed.body);
            if (action == OnvifAction::kUnknown) {
                IncrementParseFailures();
                status = 400;
                reason = "Bad Request";
                body = SoapFault("unsupported onvif action");
            } else {
                if (!AuthorizeOnvifRequest(dependencies_.auth_service,
                                           options_.enable_auth,
                                           parsed.headers, action)) {
                    IncrementAuthFailures();
                    status = 401;
                    reason = "Unauthorized";
                    extra_headers =
                        "WWW-Authenticate: Basic realm=\"onvif\"\r\n";
                    body = SoapFault("unauthorized");
                } else {
                    body = HandleSoapAction(action, parsed.body, &status,
                                            &reason);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.soap_requests;
        }
        PublishRequestEvent(action);

        const std::string response =
            HttpResponse(status, reason, body, extra_headers);
        static_cast<void>(dependencies_.net_engine->Send(
            connection_id, reinterpret_cast<const uint8_t*>(response.data()),
            response.size()));
        static_cast<void>(dependencies_.net_engine->CloseAfterSend(connection_id));
    }

    std::string HandleSoapAction(OnvifAction action,
                                 const std::string& request,
                                 uint32_t* status,
                                 std::string* reason) {
        switch (action) {
            case OnvifAction::kGetDeviceInformation:
                return SoapEnvelope(DeviceInformationBody(
                    options_, dependencies_.system_service));
            case OnvifAction::kGetSystemDateAndTime:
                return SoapEnvelope(
                    SystemDateAndTimeBody(dependencies_.time_service));
            case OnvifAction::kSetSystemDateAndTime:
                return SoapEnvelope(SetSystemDateAndTimeBody(
                    dependencies_.time_service, request, status, reason));
            case OnvifAction::kGetProfiles:
                return SoapEnvelope(ProfilesBody(&uri_provider_));
            case OnvifAction::kGetStreamUri: {
                StreamId stream_id = StreamId::kMain;
                if (!ParseStreamId(request, &stream_id)) {
                    return SoapEnvelope(ProfileFault(status, reason));
                }
                OnvifBodyResult result = onvif_internal::StreamUriBody(
                    &uri_provider_, stream_id, status, reason);
                if (result.success) {
                    IncrementStreamUriRequests();
                }
                return SoapEnvelope(result.body);
            }
            case OnvifAction::kGetSnapshotUri: {
                StreamId stream_id = StreamId::kMain;
                if (!ParseStreamId(request, &stream_id)) {
                    return SoapEnvelope(ProfileFault(status, reason));
                }
                OnvifBodyResult result = onvif_internal::SnapshotUriBody(
                    &uri_provider_, stream_id, status, reason);
                if (result.success) {
                    IncrementSnapshotUriRequests();
                }
                return SoapEnvelope(result.body);
            }
            case OnvifAction::kUnknown:
                break;
        }
        if (status != nullptr) {
            *status = 400;
        }
        if (reason != nullptr) {
            *reason = "Bad Request";
        }
        return SoapFault("unsupported onvif action");
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

    void PublishRequestEvent(OnvifAction action) {
        if (dependencies_.event_service == nullptr) {
            return;
        }
        Event event;
        event.type = EventType::kOnvifRequestReceived;
        event.source = "onvif_service";
        event.message = ActionName(action);
        static_cast<void>(dependencies_.event_service->Publish(event));
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
            (void)dependencies_.net_engine->CloseUdp(udp_socket_id_);
            udp_socket_id_ = 0;
        }
        if (tcp_server_id_ != 0 && dependencies_.net_engine != nullptr) {
            (void)dependencies_.net_engine->CloseTcp(tcp_server_id_);
            tcp_server_id_ = 0;
        }
    }

    OnvifServiceOptions options_;
    OnvifServiceDependencies dependencies_;
    ServiceUriProvider uri_provider_;
    TcpServerId tcp_server_id_ = 0;
    UdpSocketId udp_socket_id_ = 0;
    mutable std::mutex mutex_;
    OnvifServiceStats stats_;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace

std::unique_ptr<IOnvifService> CreateOnvifService(
    const OnvifServiceOptions& options,
    const OnvifServiceDependencies& dependencies) {
    return std::unique_ptr<IOnvifService>(
        new OnvifServiceImpl(options, dependencies));
}

const char* OnvifService::Name() {
    return "onvif_service";
}

}  // namespace live_stream
