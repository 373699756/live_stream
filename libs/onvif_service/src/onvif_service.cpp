#include "onvif_service.h"

#include "auth_service.h"
#include "event_service.h"
#include "netframe_service.h"
#include "system_service.h"
#include "time_service.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>

namespace live_stream {
namespace {

constexpr uint32_t kMaxHttpResponseBytes = 32 * 1024;

enum class OnvifAction {
    kUnknown,
    kGetDeviceInformation,
    kGetSystemDateAndTime,
    kSetSystemDateAndTime,
    kGetProfiles,
    kGetStreamUri,
    kGetSnapshotUri,
};

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return value;
}

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::string XmlEscape(const std::string& value) {
    std::string escaped;
    for (char c : value) {
        switch (c) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            default:
                escaped.push_back(c);
                break;
        }
    }
    return escaped;
}

std::string HttpResponse(uint32_t status_code,
                         const std::string& reason,
                         const std::string& body,
                         const std::string& extra_headers) {
    std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " +
                           reason + "\r\n";
    response += "Content-Type: application/soap+xml; charset=utf-8\r\n";
    response += extra_headers;
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

std::string SoapEnvelope(const std::string& body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
           "<s:Body>" +
           body + "</s:Body></s:Envelope>";
}

std::string SoapFault(const std::string& reason) {
    return SoapEnvelope("<s:Fault><s:Reason><s:Text>" + XmlEscape(reason) +
                        "</s:Text></s:Reason></s:Fault>");
}

struct HttpRequest {
    std::string method;
    std::string path;
    std::string headers;
    std::string body;
};

infra::Result<HttpRequest> ParseHttpRequest(const std::string& raw) {
    const std::size_t line_end = raw.find("\r\n");
    if (line_end == std::string::npos) {
        return infra::Result<HttpRequest>::Fail(infra::Status::kInvalidParam);
    }
    const std::string request_line = raw.substr(0, line_end);
    const std::size_t first_space = request_line.find(' ');
    const std::size_t second_space =
        first_space == std::string::npos
            ? std::string::npos
            : request_line.find(' ', first_space + 1);
    if (first_space == std::string::npos || second_space == std::string::npos) {
        return infra::Result<HttpRequest>::Fail(infra::Status::kInvalidParam);
    }

    const std::size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return infra::Result<HttpRequest>::Fail(infra::Status::kInvalidParam);
    }

    HttpRequest request;
    request.method = request_line.substr(0, first_space);
    request.path =
        request_line.substr(first_space + 1, second_space - first_space - 1);
    request.headers = raw.substr(line_end + 2, header_end - line_end - 2);
    request.body = raw.substr(header_end + 4);
    if (request.method.empty() || request.path.empty()) {
        return infra::Result<HttpRequest>::Fail(infra::Status::kInvalidParam);
    }
    return infra::Result<HttpRequest>::Ok(request);
}

OnvifAction ParseAction(const std::string& body) {
    if (Contains(body, "GetDeviceInformation")) {
        return OnvifAction::kGetDeviceInformation;
    }
    if (Contains(body, "GetSystemDateAndTime")) {
        return OnvifAction::kGetSystemDateAndTime;
    }
    if (Contains(body, "SetSystemDateAndTime")) {
        return OnvifAction::kSetSystemDateAndTime;
    }
    if (Contains(body, "GetProfiles")) {
        return OnvifAction::kGetProfiles;
    }
    if (Contains(body, "GetStreamUri")) {
        return OnvifAction::kGetStreamUri;
    }
    if (Contains(body, "GetSnapshotUri")) {
        return OnvifAction::kGetSnapshotUri;
    }
    return OnvifAction::kUnknown;
}

infra::Result<infra::StreamId> ParseStreamId(const std::string& body) {
    const std::string begin_tag = "<ProfileToken>";
    const std::string end_tag = "</ProfileToken>";
    const std::size_t begin = body.find(begin_tag);
    if (begin != std::string::npos) {
        const std::size_t token_begin = begin + begin_tag.size();
        const std::size_t end = body.find(end_tag, token_begin);
        if (end != std::string::npos) {
            const std::string token =
                ToLower(body.substr(token_begin, end - token_begin));
            if (token == "sub" || token == "profile_sub") {
                return infra::Result<infra::StreamId>::Ok(
                    infra::StreamId::kSub);
            }
            if (token == "main" || token == "profile_main") {
                return infra::Result<infra::StreamId>::Ok(
                    infra::StreamId::kMain);
            }
            return infra::Result<infra::StreamId>::Fail(
                infra::Status::kInvalidParam);
        }
        return infra::Result<infra::StreamId>::Fail(infra::Status::kInvalidParam);
    }
    return infra::Result<infra::StreamId>::Ok(infra::StreamId::kMain);
}

infra::Result<int64_t> ExtractInt64Tag(const std::string& text,
                                       const std::string& tag) {
    const std::string begin_tag = "<" + tag + ">";
    const std::string end_tag = "</" + tag + ">";
    const std::size_t begin = text.find(begin_tag);
    if (begin == std::string::npos) {
        return infra::Result<int64_t>::Fail(infra::Status::kNotFound);
    }
    const std::size_t value_begin = begin + begin_tag.size();
    const std::size_t end = text.find(end_tag, value_begin);
    if (end == std::string::npos) {
        return infra::Result<int64_t>::Fail(infra::Status::kInvalidParam);
    }
    const std::string raw = text.substr(value_begin, end - value_begin);
    char* parse_end = nullptr;
    const long long parsed = std::strtoll(raw.c_str(), &parse_end, 10);
    if (parse_end == raw.c_str() || *parse_end != '\0') {
        return infra::Result<int64_t>::Fail(infra::Status::kInvalidParam);
    }
    return infra::Result<int64_t>::Ok(static_cast<int64_t>(parsed));
}

int64_t DaysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy =
        (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 +
           static_cast<int64_t>(doe) - 719468;
}

infra::Result<int64_t> ParseOnvifUnixTimeMs(const std::string& request) {
    infra::Result<int64_t> unix_ms = ExtractInt64Tag(request, "tt:UnixTimeMs");
    if (unix_ms.IsOk()) {
        return unix_ms;
    }
    unix_ms = ExtractInt64Tag(request, "UnixTimeMs");
    if (unix_ms.IsOk()) {
        return unix_ms;
    }

    infra::Result<int64_t> year = ExtractInt64Tag(request, "tt:Year");
    infra::Result<int64_t> month = ExtractInt64Tag(request, "tt:Month");
    infra::Result<int64_t> day = ExtractInt64Tag(request, "tt:Day");
    infra::Result<int64_t> hour = ExtractInt64Tag(request, "tt:Hour");
    infra::Result<int64_t> minute = ExtractInt64Tag(request, "tt:Minute");
    infra::Result<int64_t> second = ExtractInt64Tag(request, "tt:Second");
    if (!year.IsOk()) {
        year = ExtractInt64Tag(request, "Year");
    }
    if (!month.IsOk()) {
        month = ExtractInt64Tag(request, "Month");
    }
    if (!day.IsOk()) {
        day = ExtractInt64Tag(request, "Day");
    }
    if (!hour.IsOk()) {
        hour = ExtractInt64Tag(request, "Hour");
    }
    if (!minute.IsOk()) {
        minute = ExtractInt64Tag(request, "Minute");
    }
    if (!second.IsOk()) {
        second = ExtractInt64Tag(request, "Second");
    }
    if (!year.IsOk() || !month.IsOk() || !day.IsOk() || !hour.IsOk() ||
        !minute.IsOk() || !second.IsOk() || year.value < 1970 ||
        month.value < 1 || month.value > 12 || day.value < 1 ||
        day.value > 31 || hour.value < 0 || hour.value > 23 ||
        minute.value < 0 || minute.value > 59 || second.value < 0 ||
        second.value > 60) {
        return infra::Result<int64_t>::Fail(infra::Status::kInvalidParam);
    }

    const int64_t days = DaysFromCivil(
        static_cast<int>(year.value), static_cast<unsigned>(month.value),
        static_cast<unsigned>(day.value));
    const int64_t seconds = days * 86400 + hour.value * 3600 +
                            minute.value * 60 + second.value;
    return infra::Result<int64_t>::Ok(seconds * 1000);
}

std::string StreamToken(infra::StreamId stream_id) {
    return stream_id == infra::StreamId::kSub ? "profile_sub" : "profile_main";
}

std::string Base64Decode(const std::string& encoded) {
    static const std::string kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded;
    int value = 0;
    int bits = -8;
    for (char c : encoded) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            continue;
        }
        if (c == '=') {
            break;
        }
        const std::size_t index = kAlphabet.find(c);
        if (index == std::string::npos) {
            return "";
        }
        value = (value << 6) + static_cast<int>(index);
        bits += 6;
        if (bits >= 0) {
            decoded.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return decoded;
}

bool ExtractBasicCredentials(const std::string& request,
                             std::string* user_name,
                             std::string* password) {
    const std::string lower = ToLower(request);
    const std::string marker = "authorization: basic ";
    const std::size_t begin = lower.find(marker);
    if (begin == std::string::npos || user_name == nullptr ||
        password == nullptr) {
        return false;
    }
    std::size_t value_begin = begin + marker.size();
    std::size_t value_end = request.find("\r\n", value_begin);
    if (value_end == std::string::npos) {
        value_end = request.find('\n', value_begin);
    }
    if (value_end == std::string::npos) {
        value_end = request.size();
    }
    const std::string decoded =
        Base64Decode(request.substr(value_begin, value_end - value_begin));
    const std::size_t separator = decoded.find(':');
    if (separator == std::string::npos) {
        return false;
    }
    *user_name = decoded.substr(0, separator);
    *password = decoded.substr(separator + 1);
    return true;
}

AuthPermission PermissionForAction(OnvifAction action) {
    switch (action) {
        case OnvifAction::kGetStreamUri:
        case OnvifAction::kGetSnapshotUri:
            return AuthPermission::kPreviewVideo;
        case OnvifAction::kSetSystemDateAndTime:
            return AuthPermission::kModifyConfig;
        case OnvifAction::kGetDeviceInformation:
        case OnvifAction::kGetSystemDateAndTime:
        case OnvifAction::kGetProfiles:
        case OnvifAction::kUnknown:
            return AuthPermission::kReadStatus;
    }
    return AuthPermission::kReadStatus;
}

class OnvifServiceImpl : public IOnvifService {
 public:
    OnvifServiceImpl(const OnvifServiceOptions& options,
                     const OnvifServiceDependencies& dependencies)
        : options_(options), dependencies_(dependencies) {}

    infra::Status Init() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return infra::Status::kOk;
        }
        if (dependencies_.net_engine == nullptr ||
            options_.device_service_port == 0 ||
            options_.discovery_port == 0 ||
            options_.max_request_bytes == 0 ||
            options_.max_request_bytes > kMaxHttpResponseBytes ||
            options_.service_path.empty()) {
            return infra::Status::kInvalidParam;
        }
        if (options_.enable_auth && dependencies_.auth_service == nullptr) {
            return infra::Status::kInvalidParam;
        }
        initialized_ = true;
        return infra::Status::kOk;
    }

    infra::Status Start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return infra::Status::kBusy;
        }
        if (started_) {
            return infra::Status::kOk;
        }

        TcpListenOptions tcp_config;
        tcp_config.address.ip = options_.listen_ip;
        tcp_config.address.port = options_.device_service_port;
        tcp_config.max_connections = 16;
        TcpCallbacks tcp_callbacks;
        tcp_callbacks.user = this;
        tcp_callbacks.on_read = &OnvifServiceImpl::HandleTcpRead;
        infra::Result<TcpServerId> tcp_result =
            dependencies_.net_engine->ListenTcp(tcp_config, tcp_callbacks);
        if (!tcp_result.IsOk()) {
            return tcp_result.status;
        }
        tcp_server_id_ = tcp_result.value;

        if (options_.discovery_enabled) {
            UdpBindOptions udp_config;
            udp_config.address.ip = options_.listen_ip;
            udp_config.address.port = options_.discovery_port;
            UdpCallbacks udp_callbacks;
            udp_callbacks.user = this;
            udp_callbacks.on_read = &OnvifServiceImpl::HandleUdpRead;
            infra::Result<UdpSocketId> udp_result =
                dependencies_.net_engine->BindUdp(udp_config, udp_callbacks);
            if (!udp_result.IsOk()) {
                return udp_result.status;
            }
            udp_socket_id_ = udp_result.value;
        }

        infra::Status error = dependencies_.net_engine->Start();
        if (error != infra::Status::kOk) {
            return error;
        }
        started_ = true;
        return infra::Status::kOk;
    }

    void Stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (udp_socket_id_ != 0 && dependencies_.net_engine != nullptr) {
            (void)dependencies_.net_engine->CloseUdp(udp_socket_id_);
            udp_socket_id_ = 0;
        }
        if (tcp_server_id_ != 0 && dependencies_.net_engine != nullptr) {
            (void)dependencies_.net_engine->CloseTcp(tcp_server_id_);
            tcp_server_id_ = 0;
        }
        started_ = false;
    }

    void Deinit() override {
        Stop();
        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = false;
    }

    const char* Name() const override { return "onvif_service"; }

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
            SoapEnvelope("<d:ProbeMatches xmlns:d=\"http://schemas.xmlsoap.org/"
                         "ws/2005/04/discovery\"><d:ProbeMatch><d:Types>"
                         "dn:NetworkVideoTransmitter</d:Types><d:XAddrs>http://" +
                         AdvertiseIp() + ":" +
                         std::to_string(options_.device_service_port) +
                         options_.service_path +
                         "</d:XAddrs></d:ProbeMatch></d:ProbeMatches>");
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
        infra::Result<HttpRequest> parsed = ParseHttpRequest(request);
        OnvifAction action = OnvifAction::kUnknown;
        std::string body;
        std::string extra_headers;
        uint32_t status = 200;
        std::string reason = "OK";
        if (!parsed.IsOk() || parsed.value.method != "POST" ||
            parsed.value.path != options_.service_path) {
            IncrementParseFailures();
            status = 400;
            reason = "Bad Request";
            body = SoapFault("invalid onvif http request");
        } else {
            action = ParseAction(parsed.value.body);
            if (action == OnvifAction::kUnknown) {
                IncrementParseFailures();
                status = 400;
                reason = "Bad Request";
                body = SoapFault("unsupported onvif action");
            } else {
                const infra::Status auth_error =
                    Authorize(parsed.value.headers, action);
                if (auth_error != infra::Status::kOk) {
                    IncrementAuthFailures();
                    status = 401;
                    reason = "Unauthorized";
                    extra_headers =
                        "WWW-Authenticate: Basic realm=\"onvif\"\r\n";
                    body = SoapFault("unauthorized");
                } else {
                    body = HandleSoapAction(action, parsed.value.body, &status,
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

    infra::Status Authorize(const std::string& headers, OnvifAction action) {
        if (!options_.enable_auth) {
            return infra::Status::kOk;
        }
        std::string user_name;
        std::string password;
        if (!ExtractBasicCredentials(headers, &user_name, &password)) {
            return infra::Status::kUnauthorized;
        }
        LoginRequest login;
        login.context.client_ip = "onvif";
        login.user_name = user_name;
        login.password = password;
        infra::Result<LoginResult> result = dependencies_.auth_service->Login(login);
        if (!result.IsOk()) {
            return result.status;
        }
        const infra::Status permission_error =
            dependencies_.auth_service->CheckPermission(
            result.value.principal, PermissionForAction(action),
            "onvif_service");
        infra::RequestContext logout_context;
        logout_context.user_name = result.value.principal.user_name;
        logout_context.session_id = result.value.principal.session_id;
        static_cast<void>(dependencies_.auth_service->Logout(logout_context));
        return permission_error;
    }

    std::string HandleSoapAction(OnvifAction action,
                                 const std::string& request,
                                 uint32_t* status,
                                 std::string* reason) {
        switch (action) {
            case OnvifAction::kGetDeviceInformation:
                return SoapEnvelope(DeviceInformationBody());
            case OnvifAction::kGetSystemDateAndTime:
                return SoapEnvelope(SystemDateAndTimeBody());
            case OnvifAction::kSetSystemDateAndTime:
                return SoapEnvelope(SetSystemDateAndTimeBody(request, status,
                                                             reason));
            case OnvifAction::kGetProfiles:
                return SoapEnvelope(ProfilesBody());
            case OnvifAction::kGetStreamUri: {
                infra::Result<infra::StreamId> stream_id =
                    ParseStreamId(request);
                if (!stream_id.IsOk()) {
                    return SoapEnvelope(ProfileFault(status, reason));
                }
                return SoapEnvelope(StreamUriBody(stream_id.value, status,
                                                  reason));
            }
            case OnvifAction::kGetSnapshotUri: {
                infra::Result<infra::StreamId> stream_id =
                    ParseStreamId(request);
                if (!stream_id.IsOk()) {
                    return SoapEnvelope(ProfileFault(status, reason));
                }
                return SoapEnvelope(SnapshotUriBody(stream_id.value, status,
                                                    reason));
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

    std::string ProfileFault(uint32_t* status, std::string* reason) const {
        if (status != nullptr) {
            *status = 400;
        }
        if (reason != nullptr) {
            *reason = "Bad Request";
        }
        return SoapFault("unknown profile token");
    }

    std::string DeviceInformationBody() const {
        DeviceInfo info;
        std::string manufacturer = options_.manufacturer;
        info.model = options_.model;
        info.firmware_version = options_.firmware_version;
        if (dependencies_.system_service != nullptr) {
            infra::Result<DeviceInfo> result =
                dependencies_.system_service->GetDeviceInfo();
            if (result.IsOk()) {
                info = result.value;
            }
        }
        return "<tds:GetDeviceInformationResponse>"
               "<tds:Manufacturer>" +
               XmlEscape(manufacturer) +
               "</tds:Manufacturer><tds:Model>" + XmlEscape(info.model) +
               "</tds:Model><tds:FirmwareVersion>" +
               XmlEscape(info.firmware_version) +
               "</tds:FirmwareVersion><tds:SerialNumber>" +
               XmlEscape(info.serial_number) +
               "</tds:SerialNumber></tds:GetDeviceInformationResponse>";
    }

    std::string SystemDateAndTimeBody() const {
        TimeStatus status;
        if (dependencies_.time_service != nullptr) {
            infra::Result<TimeStatus> result =
                dependencies_.time_service->GetTimeStatus();
            if (result.IsOk()) {
                status = result.value;
            }
        }
        return "<tds:GetSystemDateAndTimeResponse><tds:SystemDateAndTime>"
               "<tds:TimeZone><tt:TZ>" +
               XmlEscape(status.timezone) +
               "</tt:TZ></tds:TimeZone><tds:UTCDateTime><tt:UnixTimeMs>" +
               std::to_string(status.system_time_ms) +
               "</tt:UnixTimeMs></tds:UTCDateTime></tds:SystemDateAndTime>"
               "</tds:GetSystemDateAndTimeResponse>";
    }

    std::string SetSystemDateAndTimeBody(const std::string& request,
                                         uint32_t* status,
                                         std::string* reason) {
        if (dependencies_.time_service == nullptr) {
            if (status != nullptr) {
                *status = 500;
            }
            if (reason != nullptr) {
                *reason = "Internal Server Status";
            }
            return SoapFault("time service unavailable");
        }
        infra::Result<int64_t> unix_time_ms = ParseOnvifUnixTimeMs(request);
        if (!unix_time_ms.IsOk()) {
            if (status != nullptr) {
                *status = 400;
            }
            if (reason != nullptr) {
                *reason = "Bad Request";
            }
            return SoapFault("invalid date time");
        }
        infra::RequestContext context;
        context.user_name = "onvif";
        const infra::Status error = dependencies_.time_service->SetSystemTime(
            context, unix_time_ms.value, TimeSyncSource::kOnvif);
        if (error != infra::Status::kOk) {
            if (status != nullptr) {
                *status = 500;
            }
            if (reason != nullptr) {
                *reason = "Internal Server Status";
            }
            return SoapFault("time sync failed");
        }
        return "<tds:SetSystemDateAndTimeResponse/>";
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

    std::string ProfilesBody() const {
        return "<trt:GetProfilesResponse>"
               "<trt:Profiles token=\"profile_main\"><tt:Name>MainStream"
               "</tt:Name></trt:Profiles>"
               "<trt:Profiles token=\"profile_sub\"><tt:Name>SubStream"
               "</tt:Name></trt:Profiles></trt:GetProfilesResponse>";
    }

    std::string StreamUriBody(infra::StreamId stream_id,
                              uint32_t* status,
                              std::string* reason) {
        if (dependencies_.uri_provider == nullptr) {
            return UriFault(status, reason);
        }
        infra::Result<std::string> uri =
            dependencies_.uri_provider->GetStreamUri(stream_id);
        if (!uri.IsOk()) {
            return UriFault(status, reason);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.stream_uri_requests;
        }
        return "<trt:GetStreamUriResponse><trt:MediaUri><tt:Uri>" +
               XmlEscape(uri.value) +
               "</tt:Uri></trt:MediaUri><trt:ProfileToken>" +
               StreamToken(stream_id) +
               "</trt:ProfileToken></trt:GetStreamUriResponse>";
    }

    std::string SnapshotUriBody(infra::StreamId stream_id,
                                uint32_t* status,
                                std::string* reason) {
        if (dependencies_.uri_provider == nullptr) {
            return UriFault(status, reason);
        }
        infra::Result<std::string> uri =
            dependencies_.uri_provider->GetSnapshotUri(stream_id);
        if (!uri.IsOk()) {
            return UriFault(status, reason);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.snapshot_uri_requests;
        }
        return "<trt:GetSnapshotUriResponse><trt:MediaUri><tt:Uri>" +
               XmlEscape(uri.value) +
               "</tt:Uri></trt:MediaUri><trt:ProfileToken>" +
               StreamToken(stream_id) +
               "</trt:ProfileToken></trt:GetSnapshotUriResponse>";
    }

    std::string UriFault(uint32_t* status, std::string* reason) const {
        if (status != nullptr) {
            *status = 500;
        }
        if (reason != nullptr) {
            *reason = "Internal Server Status";
        }
        return SoapFault("uri unavailable");
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

    const char* ActionName(OnvifAction action) const {
        switch (action) {
            case OnvifAction::kGetDeviceInformation:
                return "GetDeviceInformation";
            case OnvifAction::kGetSystemDateAndTime:
                return "GetSystemDateAndTime";
            case OnvifAction::kSetSystemDateAndTime:
                return "SetSystemDateAndTime";
            case OnvifAction::kGetProfiles:
                return "GetProfiles";
            case OnvifAction::kGetStreamUri:
                return "GetStreamUri";
            case OnvifAction::kGetSnapshotUri:
                return "GetSnapshotUri";
            case OnvifAction::kUnknown:
                return "Unknown";
        }
        return "Unknown";
    }

    void IncrementParseFailures() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.parse_failures;
    }

    void IncrementAuthFailures() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.auth_failures;
    }

    OnvifServiceOptions options_;
    OnvifServiceDependencies dependencies_;
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
