#include "http_service.h"

#include "auth_service.h"
#include "config_service.h"
#include "infra/time.h"
#include "infra/executor.h"
#include "infra/fs.h"
#include "infra/sync.h"
#include "logger_service.h"
#include "media_service.h"
#include "netframe_service.h"

#include "../../../3rdparty/nlohmann_json.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char* kModuleName = "http_service";
using Json = nlohmann::json;

std::string ToLower(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string Trim(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

const char* HttpStatusText(int status_code) {
    switch (status_code) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 409:
            return "Conflict";
        case 413:
            return "Payload Too Large";
        case 500:
            return "Internal Server Status";
        case 501:
            return "Not Implemented";
        case 503:
            return "Service Unavailable";
        default:
            return "Status";
    }
}

int HttpStatusFromInfraStatus(infra::Status status) {
    switch (status) {
        case infra::Status::kOk:
            return 200;
        case infra::Status::kInvalidParam:
            return 400;
        case infra::Status::kUnauthorized:
            return 401;
        case infra::Status::kNoPermission:
            return 403;
        case infra::Status::kNotFound:
            return 404;
        case infra::Status::kAlreadyExists:
        case infra::Status::kBusy:
            return 409;
        case infra::Status::kNotSupported:
            return 501;
        case infra::Status::kTimeout:
            return 503;
        case infra::Status::kNoMemory:
        case infra::Status::kIoError:
        case infra::Status::kInternalError:
            return 500;
    }
    return 500;
}

HttpResponse JsonResponse(int status_code, const Json& value) {
    HttpResponse response;
    response.status_code = status_code;
    response.headers["Content-Type"] = "application/json";
    response.body = value.dump();
    return response;
}

HttpResponse StatusResponse(int status_code, const std::string& reason) {
    Json root = Json::object();
    root["error"] = reason;
    return JsonResponse(status_code, root);
}

HttpResponse StatusToResponse(infra::Status status) {
    return StatusResponse(HttpStatusFromInfraStatus(status),
                         infra::StatusToString(status));
}

HttpResponse OkResponse() {
    Json root = Json::object();
    root["ok"] = true;
    return JsonResponse(200, root);
}

std::string GetHeader(const HttpRequest& request, const std::string& name) {
    const std::string lower_name = ToLower(name);
    for (const auto& item : request.headers) {
        if (ToLower(item.first) == lower_name) {
            return item.second;
        }
    }
    return std::string();
}

std::string RequestUserAgent(const HttpRequest& request) {
    return GetHeader(request, "User-Agent");
}

std::string MakeRequestId(uint64_t id) {
    return std::string("http-") + std::to_string(infra::Time::SystemTimeMillis()) +
           "-" + std::to_string(id);
}

bool GetObjectString(const Json& object,
                     const std::string& key,
                     std::string* value) {
    if (value == nullptr || !object.is_object() || !object.contains(key) ||
        !object.at(key).is_string()) {
        return false;
    }
    *value = object.at(key).get<std::string>();
    return true;
}

bool GetObjectInt(const Json& object,
                  const std::string& key,
                  int64_t* value) {
    if (value == nullptr || !object.is_object() || !object.contains(key) ||
        !object.at(key).is_number_integer()) {
        return false;
    }
    *value = object.at(key).get<int64_t>();
    return true;
}

bool GetObjectBoolOrDefault(const Json& object,
                            const std::string& key,
                            bool default_value,
                            bool* value) {
    if (value == nullptr || !object.is_object()) {
        return false;
    }
    if (!object.contains(key)) {
        *value = default_value;
        return true;
    }
    if (!object.at(key).is_boolean()) {
        return false;
    }
    *value = object.at(key).get<bool>();
    return true;
}

bool ParseUint32Strict(const std::string& value, uint32_t* out) {
    if (out == nullptr || value.empty()) {
        return false;
    }
    uint64_t parsed = 0;
    for (char c : value) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
        parsed = parsed * 10 + static_cast<uint64_t>(c - '0');
        if (parsed > 4294967295ULL) {
            return false;
        }
    }
    *out = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseResolutionValue(const std::string& value, VideoResolution* resolution) {
    if (resolution == nullptr) {
        return false;
    }
    const size_t separator = value.find('x');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= value.size()) {
        return false;
    }
    uint32_t width = 0;
    uint32_t height = 0;
    if (!ParseUint32Strict(value.substr(0, separator), &width) ||
        !ParseUint32Strict(value.substr(separator + 1), &height) ||
        width == 0 || height == 0) {
        return false;
    }
    resolution->width = width;
    resolution->height = height;
    return true;
}

bool ContainsCodec(const VideoStreamCapabilities& capabilities,
                   infra::VideoCodec codec) {
    for (const CodecCapability& item : capabilities.codecs) {
        if (item.codec == codec) {
            return true;
        }
    }
    return false;
}

bool ContainsResolution(const VideoStreamCapabilities& capabilities,
                        const VideoResolution& resolution) {
    for (const VideoResolution& item : capabilities.resolutions) {
        if (item.width == resolution.width && item.height == resolution.height) {
            return true;
        }
    }
    return false;
}

bool ContainsRateControl(const VideoStreamCapabilities& capabilities,
                         RateControlMode mode) {
    for (RateControlMode item : capabilities.rate_control_modes) {
        if (item == mode) {
            return true;
        }
    }
    return false;
}

const VideoStreamCapabilities* FindStreamCapabilities(
    const MediaCapabilities& capabilities,
    infra::StreamId stream_id) {
    for (const VideoStreamCapabilities& item : capabilities.streams) {
        if (item.stream_id == stream_id) {
            return &item;
        }
    }
    return nullptr;
}

std::string ExtractBearerToken(const HttpRequest& request) {
    const std::string authorization = Trim(GetHeader(request, "Authorization"));
    const std::string prefix = "bearer ";
    if (authorization.size() <= prefix.size()) {
        return std::string();
    }
    if (ToLower(authorization.substr(0, prefix.size())) != prefix) {
        return std::string();
    }
    return Trim(authorization.substr(prefix.size()));
}

std::string AuthRoleToJsonString(AuthRole role) {
    return AuthRoleToString(role);
}

const char* StreamIdToJsonString(infra::StreamId stream_id) {
    switch (stream_id) {
        case infra::StreamId::kMain:
            return "main";
        case infra::StreamId::kSub:
            return "sub";
        case infra::StreamId::kSnapshot:
            return "snapshot";
    }
    return "unknown";
}

infra::Result<infra::StreamId> StreamIdFromJsonString(const std::string& value) {
    if (value == "main") {
        return infra::Result<infra::StreamId>::Ok(infra::StreamId::kMain);
    }
    if (value == "sub") {
        return infra::Result<infra::StreamId>::Ok(infra::StreamId::kSub);
    }
    return infra::Result<infra::StreamId>::Fail(infra::Status::kInvalidParam);
}

const char* VideoCodecToJsonString(infra::VideoCodec codec) {
    switch (codec) {
        case infra::VideoCodec::kH264:
            return "h264";
        case infra::VideoCodec::kH265:
            return "h265";
        case infra::VideoCodec::kMjpeg:
            return "mjpeg";
    }
    return "unknown";
}

infra::Result<infra::VideoCodec> VideoCodecFromJsonString(
    const std::string& value) {
    if (value == "h264") {
        return infra::Result<infra::VideoCodec>::Ok(infra::VideoCodec::kH264);
    }
    if (value == "h265") {
        return infra::Result<infra::VideoCodec>::Ok(infra::VideoCodec::kH265);
    }
    if (value == "mjpeg") {
        return infra::Result<infra::VideoCodec>::Ok(infra::VideoCodec::kMjpeg);
    }
    return infra::Result<infra::VideoCodec>::Fail(infra::Status::kInvalidParam);
}

const char* RateControlModeToJsonString(RateControlMode mode) {
    switch (mode) {
        case RateControlMode::kCbr:
            return "cbr";
        case RateControlMode::kVbr:
            return "vbr";
        case RateControlMode::kFixQp:
            return "fixqp";
    }
    return "unknown";
}

infra::Result<RateControlMode> RateControlModeFromJsonString(
    const std::string& value) {
    if (value == "cbr") {
        return infra::Result<RateControlMode>::Ok(RateControlMode::kCbr);
    }
    if (value == "vbr") {
        return infra::Result<RateControlMode>::Ok(RateControlMode::kVbr);
    }
    if (value == "fixqp") {
        return infra::Result<RateControlMode>::Ok(RateControlMode::kFixQp);
    }
    return infra::Result<RateControlMode>::Fail(infra::Status::kInvalidParam);
}

Json VideoResolutionToJson(const VideoResolution& resolution) {
    Json root = Json::object();
    root["width"] = resolution.width;
    root["height"] = resolution.height;
    return root;
}

Json CodecCapabilityToJson(const CodecCapability& capability) {
    Json root = Json::object();
    root["codec"] = VideoCodecToJsonString(capability.codec);
    Json profiles = Json::array();
    for (const std::string& profile : capability.profiles) {
        profiles.push_back(profile);
    }
    root["profiles"] = profiles;
    return root;
}

Json StreamCapabilitiesToJson(
    const VideoStreamCapabilities& stream) {
    Json root = Json::object();
    root["stream"] = StreamIdToJsonString(stream.stream_id);

    Json codecs = Json::array();
    for (const CodecCapability& capability : stream.codecs) {
        codecs.push_back(CodecCapabilityToJson(capability));
    }
    root["codecs"] = codecs;

    Json resolutions = Json::array();
    for (const VideoResolution& resolution : stream.resolutions) {
        resolutions.push_back(VideoResolutionToJson(resolution));
    }
    root["resolutions"] = resolutions;

    Json fps = Json::object();
    fps["min"] = stream.frame_rate.min_fps;
    fps["max"] = stream.frame_rate.max_fps;
    root["fps"] = fps;

    Json bitrate = Json::object();
    bitrate["min"] = stream.bitrate.min_kbps;
    bitrate["max"] = stream.bitrate.max_kbps;
    root["bitrate_kbps"] = bitrate;

    Json rate_control = Json::array();
    for (RateControlMode mode : stream.rate_control_modes) {
        rate_control.push_back(RateControlModeToJsonString(mode));
    }
    root["rate_control"] = rate_control;

    Json gop = Json::object();
    gop["min"] = stream.gop.min;
    gop["max"] = stream.gop.max;
    root["gop"] = gop;
    root["smart_codec"] = stream.smart_codec_supported;
    return root;
}

Json MediaCapabilitiesToJson(
    const MediaCapabilities& capabilities) {
    Json root = Json::object();
    Json streams = Json::object();
    for (const VideoStreamCapabilities& stream : capabilities.streams) {
        const char* name = StreamIdToJsonString(stream.stream_id);
        if (std::strcmp(name, "unknown") != 0) {
            streams[name] = StreamCapabilitiesToJson(stream);
        }
    }
    root["streams"] = streams;
    return root;
}

Json PrincipalToJson(const AuthPrincipal& principal) {
    Json root = Json::object();
    root["user_name"] = principal.user_name;
    root["session_id"] = principal.session_id;
    root["role"] = AuthRoleToJsonString(principal.role);
    return root;
}

Json OperationRecordToJson(const OperationRecord& record) {
    Json root = Json::object();
    root["timestamp_ms"] = record.timestamp_ms;
    root["request_id"] = record.request_id;
    root["user_name"] = record.user_name;
    root["session_id"] = record.session_id;
    root["client_ip"] = record.client_ip;
    root["module"] = record.module;
    root["action"] = OperationActionToString(record.action);
    root["target"] = record.target;
    root["result"] = OperationResultToString(record.result);
    root["reason"] = record.reason;
    return root;
}

std::string ContentTypeForPath(const std::string& path) {
    const std::string lower_path = ToLower(path);
    if (lower_path.size() >= 5 &&
        lower_path.substr(lower_path.size() - 5) == ".html") {
        return "text/html";
    }
    if (lower_path.size() >= 4 &&
        lower_path.substr(lower_path.size() - 4) == ".css") {
        return "text/css";
    }
    if (lower_path.size() >= 3 &&
        lower_path.substr(lower_path.size() - 3) == ".js") {
        return "application/javascript";
    }
    if (lower_path.size() >= 4 &&
        lower_path.substr(lower_path.size() - 4) == ".jpg") {
        return "image/jpeg";
    }
    if (lower_path.size() >= 4 &&
        lower_path.substr(lower_path.size() - 4) == ".png") {
        return "image/png";
    }
    return "application/octet-stream";
}

bool IsUnsafeStaticPath(const std::string& path) {
    const std::string lower_path = ToLower(path);
    return path.find("..") != std::string::npos ||
           path.find('\\') != std::string::npos ||
           lower_path.find("%2e") != std::string::npos ||
           lower_path.find("%5c") != std::string::npos;
}

infra::Result<HttpMethod> ParseMethod(const std::string& value) {
    if (value == "GET") {
        return infra::Result<HttpMethod>::Ok(HttpMethod::kGet);
    }
    if (value == "POST") {
        return infra::Result<HttpMethod>::Ok(HttpMethod::kPost);
    }
    if (value == "PUT") {
        return infra::Result<HttpMethod>::Ok(HttpMethod::kPut);
    }
    if (value == "DELETE") {
        return infra::Result<HttpMethod>::Ok(HttpMethod::kDelete);
    }
    return infra::Result<HttpMethod>::Fail(infra::Status::kInvalidParam);
}

std::string SerializeResponse(const HttpResponse& response) {
    std::string out = "HTTP/1.1 " + std::to_string(response.status_code) + " " +
                      HttpStatusText(response.status_code) + "\r\n";
    bool has_length = false;
    bool has_connection = false;
    for (const auto& header : response.headers) {
        if (ToLower(header.first) == "content-length") {
            has_length = true;
        }
        if (ToLower(header.first) == "connection") {
            has_connection = true;
        }
        out += header.first + ": " + header.second + "\r\n";
    }
    if (!has_length) {
        out += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    }
    if (!has_connection) {
        out += "Connection: close\r\n";
    }
    out += "\r\n";
    out += response.body;
    return out;
}

enum class RawParseStatus {
    kComplete,
    kIncomplete,
    kBadRequest,
    kPayloadTooLarge,
};

struct RawParseResult {
    RawParseStatus status = RawParseStatus::kBadRequest;
    HttpRequest request;
};

RawParseResult ParseRawRequest(const std::string& raw,
                               uint32_t max_header_bytes,
                               uint32_t max_body_bytes,
                               const std::string& client_ip) {
    const size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return RawParseResult{
            raw.size() > max_header_bytes ? RawParseStatus::kPayloadTooLarge
                                          : RawParseStatus::kIncomplete,
            HttpRequest{}};
    }
    if (header_end > max_header_bytes) {
        return RawParseResult{RawParseStatus::kPayloadTooLarge, HttpRequest{}};
    }
    const std::string header_block = raw.substr(0, header_end);
    const std::string body = raw.substr(header_end + 4);
    const size_t first_line_end = header_block.find("\r\n");
    const std::string request_line =
        first_line_end == std::string::npos
            ? header_block
            : header_block.substr(0, first_line_end);
    const size_t method_end = request_line.find(' ');
    const size_t target_end =
        method_end == std::string::npos ? std::string::npos
                                        : request_line.find(' ', method_end + 1);
    if (method_end == std::string::npos || target_end == std::string::npos) {
        return RawParseResult{RawParseStatus::kBadRequest, HttpRequest{}};
    }
    infra::Result<HttpMethod> method =
        ParseMethod(request_line.substr(0, method_end));
    if (!method.IsOk()) {
        return RawParseResult{RawParseStatus::kBadRequest, HttpRequest{}};
    }

    HttpRequest request;
    request.method = method.value;
    request.client_ip = client_ip;
    const std::string target =
        request_line.substr(method_end + 1, target_end - method_end - 1);
    const size_t query_pos = target.find('?');
    request.path = query_pos == std::string::npos ? target : target.substr(0, query_pos);
    request.query_string =
        query_pos == std::string::npos ? std::string() : target.substr(query_pos + 1);

    size_t pos = first_line_end == std::string::npos ? header_block.size()
                                                     : first_line_end + 2;
    while (pos < header_block.size()) {
        const size_t line_end = header_block.find("\r\n", pos);
        const std::string line =
            line_end == std::string::npos ? header_block.substr(pos)
                                          : header_block.substr(pos, line_end - pos);
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            return RawParseResult{RawParseStatus::kBadRequest, HttpRequest{}};
        }
        request.headers[Trim(line.substr(0, colon))] = Trim(line.substr(colon + 1));
        if (line_end == std::string::npos) {
            break;
        }
        pos = line_end + 2;
    }

    const std::string content_length_text = GetHeader(request, "Content-Length");
    size_t expected_body_size = 0;
    if (!content_length_text.empty()) {
        char* end = nullptr;
        const unsigned long parsed =
            std::strtoul(content_length_text.c_str(), &end, 10);
        if (end == nullptr || *end != '\0') {
            return RawParseResult{RawParseStatus::kBadRequest, HttpRequest{}};
        }
        expected_body_size = static_cast<size_t>(parsed);
        if (expected_body_size > max_body_bytes) {
            return RawParseResult{RawParseStatus::kPayloadTooLarge, HttpRequest{}};
        }
        if (body.size() < expected_body_size) {
            return RawParseResult{RawParseStatus::kIncomplete, HttpRequest{}};
        }
        request.body = body.substr(0, expected_body_size);
    } else if (body.size() > max_body_bytes) {
        return RawParseResult{RawParseStatus::kPayloadTooLarge, HttpRequest{}};
    }
    return RawParseResult{RawParseStatus::kComplete, request};
}

}  // namespace

class HttpServiceImpl : public IHttpService {
 public:
    HttpServiceImpl(const HttpServiceOptions& options,
                    const HttpServiceDependencies& dependencies)
        : options_(options),
          dependencies_(dependencies) {}

    ~HttpServiceImpl() override {
        Stop();
        Deinit();
    }

    infra::Status Init() override {
        infra::MutexGuard guard(&mutex_);
        if (initialized_) {
            return infra::Status::kOk;
        }
        if (dependencies_.auth_service == nullptr ||
            dependencies_.config_service == nullptr) {
            return infra::Status::kInvalidParam;
        }
        if (options_.max_request_header_bytes == 0 ||
            options_.max_request_body_bytes == 0 ||
            options_.max_connections == 0 ||
            options_.request_timeout_ms == 0 ||
            options_.executor_queue_capacity == 0) {
            return infra::Status::kInvalidParam;
        }
        task_executor_.reset(new infra::Executor());
        initialized_ = true;
        return infra::Status::kOk;
    }

    infra::Status Start() override {
        infra::Executor* task_executor = nullptr;
        {
            infra::MutexGuard guard(&mutex_);
            if (!initialized_) {
                return infra::Status::kInternalError;
            }
            if (started_) {
                return infra::Status::kOk;
            }
            if (dependencies_.net_engine == nullptr) {
                return infra::Status::kNotSupported;
            }
            task_executor = task_executor_.get();
        }
        infra::ExecutorOptions executor_options;
        executor_options.worker_count = 1;
        executor_options.queue_capacity = options_.executor_queue_capacity;
        infra::Status executor_start = task_executor->Start(executor_options);
        if (executor_start != infra::Status::kOk) {
            return executor_start;
        }

        TcpListenOptions server_config;
        server_config.address.ip = options_.listen_ip;
        server_config.address.port = options_.listen_port;
        server_config.max_connections = options_.max_connections;
        server_config.send_queue_capacity = options_.send_queue_capacity;
        TcpCallbacks callbacks;
        callbacks.user = this;
        callbacks.on_accept = &HttpServiceImpl::HandleAccept;
        callbacks.on_read = &HttpServiceImpl::HandleRead;
        callbacks.on_close = &HttpServiceImpl::HandleClose;
        infra::Result<TcpServerId> server =
            dependencies_.net_engine->ListenTcp(server_config, callbacks);
        if (!server.IsOk()) {
            task_executor->Stop(infra::StopMode::kDiscard);
            return server.status;
        }
        infra::Status start_engine = dependencies_.net_engine->Start();
        if (start_engine != infra::Status::kOk) {
            task_executor->Stop(infra::StopMode::kDiscard);
            return start_engine;
        }

        {
            infra::MutexGuard guard(&mutex_);
            tcp_server_id_ = server.value;
            started_ = true;
        }
        return infra::Status::kOk;
    }

    void Stop() override {
        TcpServerId server_id = 0;
        NetEngine* net_engine = nullptr;
        infra::Executor* task_executor = nullptr;
        {
            infra::MutexGuard guard(&mutex_);
            if (!started_) {
                return;
            }
            started_ = false;
            server_id = tcp_server_id_;
            tcp_server_id_ = 0;
            net_engine = dependencies_.net_engine;
            sessions_.clear();
            stats_.active_connections = 0;
            task_executor = task_executor_.get();
        }
        if (net_engine != nullptr && server_id != 0) {
            (void)net_engine->CloseTcp(server_id);
        }
        if (task_executor != nullptr) {
            task_executor->Stop(infra::StopMode::kDiscard);
        }
    }

    void Deinit() override {
        Stop();
        infra::MutexGuard guard(&mutex_);
        sessions_.clear();
        task_executor_.reset();
        initialized_ = false;
    }

    const char* Name() const override {
        return kModuleName;
    }

    infra::Result<HttpResponse> HandleRequest(
        const HttpRequest& request) override {
        {
            infra::MutexGuard guard(&mutex_);
            if (!initialized_) {
                return infra::Result<HttpResponse>::Fail(
                    infra::Status::kInternalError);
            }
            ++stats_.total_requests;
        }

        if (request.path.empty() || request.path[0] != '/') {
            IncrementParseFailures();
            return infra::Result<HttpResponse>::Ok(
                StatusResponse(400, "Invalid request path"));
        }

        if (request.path == "/api/auth/login" &&
            request.method == HttpMethod::kPost) {
            return infra::Result<HttpResponse>::Ok(HandleLogin(request));
        }
        if (request.path == "/api/auth/logout" &&
            request.method == HttpMethod::kPost) {
            return infra::Result<HttpResponse>::Ok(HandleLogout(request));
        }
        if (request.path == "/api/auth/me" &&
            request.method == HttpMethod::kGet) {
            return infra::Result<HttpResponse>::Ok(HandleMe(request));
        }
        if (request.path == "/api/media/capabilities" &&
            request.method == HttpMethod::kGet) {
            return infra::Result<HttpResponse>::Ok(HandleMediaCapabilities());
        }
        if (StartsWith(request.path, "/api/config/")) {
            return infra::Result<HttpResponse>::Ok(HandleConfig(request));
        }
        if (request.path == "/api/operations" &&
            request.method == HttpMethod::kGet) {
            return infra::Result<HttpResponse>::Ok(HandleOperations(request));
        }
        if (StartsWith(request.path, "/api/")) {
            return infra::Result<HttpResponse>::Ok(
                StatusResponse(501, "Not Implemented"));
        }
        if (request.method == HttpMethod::kGet && options_.enable_static_files) {
            return infra::Result<HttpResponse>::Ok(HandleStaticFile(request));
        }

        IncrementNotFound();
        return infra::Result<HttpResponse>::Ok(StatusResponse(404, "Not Found"));
    }

    HttpServiceStats GetStats() const override {
        infra::MutexGuard guard(&mutex_);
        return stats_;
    }

    infra::Result<HttpListenAddress> LocalAddress() const override {
        infra::MutexGuard guard(&mutex_);
        if (dependencies_.net_engine == nullptr || tcp_server_id_ == 0) {
            return infra::Result<HttpListenAddress>::Fail(infra::Status::kBusy);
        }
        infra::Result<NetAddress> address =
            dependencies_.net_engine->TcpLocalAddress(tcp_server_id_);
        if (!address.IsOk()) {
            return infra::Result<HttpListenAddress>::Fail(address.status);
        }
        HttpListenAddress result;
        result.ip = address.value.ip;
        result.port = address.value.port;
        return infra::Result<HttpListenAddress>::Ok(result);
    }

 private:
    static void HandleAccept(void* user, ConnectionId id, NetAddress peer) {
        HttpServiceImpl* self = static_cast<HttpServiceImpl*>(user);
        if (self != nullptr) {
            self->OnConnection(id, std::move(peer));
        }
    }

    static void HandleRead(void* user,
                           ConnectionId id,
                           const uint8_t* data,
                           size_t size) {
        HttpServiceImpl* self = static_cast<HttpServiceImpl*>(user);
        if (self != nullptr) {
            self->OnMessage(id, data, static_cast<uint32_t>(size));
        }
    }

    static void HandleClose(void* user, ConnectionId id) {
        HttpServiceImpl* self = static_cast<HttpServiceImpl*>(user);
        if (self != nullptr) {
            self->OnClose(id);
        }
    }

    static bool StartsWith(const std::string& value, const std::string& prefix) {
        return value.size() >= prefix.size() &&
               value.substr(0, prefix.size()) == prefix;
    }

    void IncrementParseFailures() {
        infra::MutexGuard guard(&mutex_);
        ++stats_.parse_failures;
    }

    void IncrementNotFound() {
        infra::MutexGuard guard(&mutex_);
        ++stats_.not_found;
    }

    void IncrementAuthFailures() {
        infra::MutexGuard guard(&mutex_);
        ++stats_.auth_failures;
    }

    void IncrementPermissionDenied() {
        infra::MutexGuard guard(&mutex_);
        ++stats_.permission_denied;
    }

    infra::RequestContext MakeContext(const HttpRequest& request,
                                      const AuthPrincipal* principal) {
        infra::RequestContext context;
        context.request_id = MakeRequestId(NextRequestId());
        context.client_ip = request.client_ip;
        context.user_agent = RequestUserAgent(request);
        if (principal != nullptr) {
            context.user_name = principal->user_name;
            context.session_id = principal->session_id;
        }
        return context;
    }

    uint64_t NextRequestId() {
        infra::MutexGuard guard(&mutex_);
        return ++next_request_id_;
    }

    infra::Result<AuthPrincipal> Authenticate(const HttpRequest& request) {
        const std::string token = ExtractBearerToken(request);
        if (token.empty()) {
            IncrementAuthFailures();
            return infra::Result<AuthPrincipal>::Fail(infra::Status::kUnauthorized);
        }
        infra::Result<TokenValidationResult> validated =
            dependencies_.auth_service->ValidateToken(token);
        if (!validated.IsOk()) {
            IncrementAuthFailures();
            return infra::Result<AuthPrincipal>::Fail(validated.status);
        }
        return infra::Result<AuthPrincipal>::Ok(validated.value.principal);
    }

    infra::Status RequirePermission(const HttpRequest& request,
                                   AuthPermission permission,
                                   const std::string& target,
                                   AuthPrincipal* principal) {
        infra::Result<AuthPrincipal> authenticated = Authenticate(request);
        if (!authenticated.IsOk()) {
            return authenticated.status;
        }
        infra::Status permission_error =
            dependencies_.auth_service->CheckPermission(
                authenticated.value, permission, target);
        if (permission_error != infra::Status::kOk) {
            IncrementPermissionDenied();
            RecordOperation(request, authenticated.value,
                            OperationAction::kPermissionDenied, target,
                            OperationResult::kRejected,
                            infra::StatusToString(permission_error));
            return permission_error;
        }
        if (principal != nullptr) {
            *principal = authenticated.value;
        }
        return infra::Status::kOk;
    }

    void RecordOperation(const HttpRequest& request,
                         const AuthPrincipal& principal,
                         OperationAction action,
                         const std::string& target,
                         OperationResult result,
                         const std::string& reason) {
        if (dependencies_.logger_service == nullptr) {
            return;
        }
        infra::RequestContext context = MakeContext(request, &principal);
        OperationRecord record;
        record.timestamp_ms = infra::Time::SystemTimeMillis();
        record.request_id = context.request_id;
        record.user_name = context.user_name;
        record.session_id = context.session_id;
        record.client_ip = context.client_ip;
        record.module = kModuleName;
        record.action = action;
        record.target = target;
        record.result = result;
        record.reason = reason;
        (void)dependencies_.logger_service->RecordOperation(record);
    }

    HttpResponse HandleLogin(const HttpRequest& request) {
        Json parsed = Json::parse(request.body, nullptr, false);
        if (parsed.is_discarded()) {
            IncrementParseFailures();
            return StatusResponse(400, "Invalid JSON");
        }
        std::string user_name;
        std::string password;
        if (!GetObjectString(parsed, "user_name", &user_name) ||
            !GetObjectString(parsed, "password", &password)) {
            return StatusResponse(400, "Invalid login request");
        }

        LoginRequest login_request;
        login_request.context = MakeContext(request, nullptr);
        login_request.user_name = user_name;
        login_request.password = password;
        infra::Result<LoginResult> login =
            dependencies_.auth_service->Login(login_request);
        if (!login.IsOk()) {
            IncrementAuthFailures();
            return StatusToResponse(login.status);
        }

        Json root = Json::object();
        root["token"] = login.value.token;
        root["expires_at_ms"] = login.value.expires_at_ms;
        root["principal"] = PrincipalToJson(login.value.principal);
        return JsonResponse(200, root);
    }

    HttpResponse HandleLogout(const HttpRequest& request) {
        infra::Result<AuthPrincipal> principal = Authenticate(request);
        if (!principal.IsOk()) {
            return StatusToResponse(principal.status);
        }
        infra::RequestContext context = MakeContext(request, &principal.value);
        infra::Status error = dependencies_.auth_service->Logout(context);
        if (error != infra::Status::kOk) {
            return StatusToResponse(error);
        }
        return OkResponse();
    }

    HttpResponse HandleMe(const HttpRequest& request) {
        infra::Result<AuthPrincipal> principal = Authenticate(request);
        if (!principal.IsOk()) {
            return StatusToResponse(principal.status);
        }
        return JsonResponse(200, PrincipalToJson(principal.value));
    }

    HttpResponse HandleMediaCapabilities() {
        if (dependencies_.media_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        infra::Result<MediaCapabilities> capabilities =
            dependencies_.media_service->GetCapabilities();
        if (!capabilities.IsOk()) {
            return StatusToResponse(capabilities.status);
        }
        return JsonResponse(200, MediaCapabilitiesToJson(capabilities.value));
    }

    std::string ValidateVideoStreamConfig(
        const Json& stream,
        const VideoStreamCapabilities& capabilities) {
        std::string codec_value;
        std::string resolution_value;
        std::string rate_control_value;
        int64_t fps = 0;
        int64_t bitrate_kbps = 0;
        int64_t gop = 0;
        bool smart_codec = false;
        if (!GetObjectString(stream, "codec", &codec_value) ||
            !GetObjectString(stream, "resolution", &resolution_value) ||
            !GetObjectString(stream, "rate_control", &rate_control_value) ||
            !GetObjectInt(stream, "fps", &fps) ||
            !GetObjectInt(stream, "bitrate_kbps", &bitrate_kbps) ||
            !GetObjectInt(stream, "gop", &gop) ||
            !GetObjectBoolOrDefault(stream, "smart_codec", false,
                                    &smart_codec)) {
            return "missing video field";
        }

        infra::Result<infra::VideoCodec> codec =
            VideoCodecFromJsonString(codec_value);
        if (!codec.IsOk() || !ContainsCodec(capabilities, codec.value)) {
            return "unsupported codec";
        }

        VideoResolution resolution;
        if (!ParseResolutionValue(resolution_value, &resolution) ||
            !ContainsResolution(capabilities, resolution)) {
            return "unsupported resolution";
        }

        if (fps < static_cast<int64_t>(capabilities.frame_rate.min_fps) ||
            fps > static_cast<int64_t>(capabilities.frame_rate.max_fps)) {
            return "unsupported fps";
        }
        if (bitrate_kbps < static_cast<int64_t>(capabilities.bitrate.min_kbps) ||
            bitrate_kbps > static_cast<int64_t>(capabilities.bitrate.max_kbps)) {
            return "unsupported bitrate_kbps";
        }
        infra::Result<RateControlMode> rate_control =
            RateControlModeFromJsonString(rate_control_value);
        if (!rate_control.IsOk() ||
            !ContainsRateControl(capabilities, rate_control.value)) {
            return "unsupported rate_control";
        }
        if (gop < static_cast<int64_t>(capabilities.gop.min) ||
            gop > static_cast<int64_t>(capabilities.gop.max)) {
            return "unsupported gop";
        }
        if (smart_codec && !capabilities.smart_codec_supported) {
            return "unsupported smart_codec";
        }
        return std::string();
    }

    std::string ValidateVideoConfigAgainstCapabilities(
        const std::string& body) {
        if (dependencies_.media_service == nullptr) {
            return std::string();
        }
        infra::Result<MediaCapabilities> capabilities =
            dependencies_.media_service->GetCapabilities();
        if (!capabilities.IsOk()) {
            return infra::StatusToString(capabilities.status);
        }

        Json parsed = Json::parse(body, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            return "invalid video config json";
        }
        if (!parsed.contains("streams") || !parsed.at("streams").is_object()) {
            return "missing streams";
        }

        const char* names[] = {"main", "sub"};
        for (const char* name : names) {
            const Json& streams = parsed.at("streams");
            if (!streams.contains(name) || !streams.at(name).is_object()) {
                return std::string("missing stream: ") + name;
            }
            infra::Result<infra::StreamId> stream_id =
                StreamIdFromJsonString(name);
            const VideoStreamCapabilities* stream_capabilities =
                stream_id.IsOk() ? FindStreamCapabilities(capabilities.value,
                                                          stream_id.value)
                                 : nullptr;
            if (stream_capabilities == nullptr) {
                return std::string("missing capabilities: ") + name;
            }
            const std::string error = ValidateVideoStreamConfig(
                streams.at(name), *stream_capabilities);
            if (!error.empty()) {
                return std::string(name) + ": " + error;
            }
        }
        return std::string();
    }

    HttpResponse HandleConfig(const HttpRequest& request) {
        const std::string name = request.path.substr(std::string("/api/config/").size());
        if (name.empty()) {
            return StatusResponse(400, "Missing config name");
        }
        if (request.method == HttpMethod::kGet) {
            infra::Result<AuthPrincipal> principal = Authenticate(request);
            if (!principal.IsOk()) {
                return StatusToResponse(principal.status);
            }
            ConfigJson config;
            infra::Status error = dependencies_.config_service->GetValue(name, &config);
            if (error != infra::Status::kOk) {
                return StatusToResponse(error);
            }
            HttpResponse response;
            response.status_code = 200;
            response.headers["Content-Type"] = "application/json";
            response.body = config.dump();
            return response;
        }
        if (request.method == HttpMethod::kPut) {
            AuthPrincipal principal;
            infra::Status permission = RequirePermission(
                request, AuthPermission::kModifyConfig, name, &principal);
            if (permission != infra::Status::kOk) {
                return StatusToResponse(permission);
            }
            if (name == "video") {
                const std::string validation_error =
                    ValidateVideoConfigAgainstCapabilities(request.body);
                if (!validation_error.empty()) {
                    return StatusResponse(400, validation_error);
                }
            }
            ConfigJson config = ConfigJson::parse(request.body, nullptr, false);
            if (config.is_discarded()) {
                return StatusResponse(400, "Invalid JSON");
            }
            infra::Status error = dependencies_.config_service->SetValue(
                name, config);
            RecordOperation(request, principal, OperationAction::kModifyConfig,
                            name,
                            error == infra::Status::kOk ? OperationResult::kSuccess
                                                       : OperationResult::kFailed,
                            error == infra::Status::kOk ? std::string()
                                                       : infra::StatusToString(error));
            if (error != infra::Status::kOk) {
                return StatusToResponse(error);
            }
            return OkResponse();
        }
        return StatusResponse(404, "Not Found");
    }

    HttpResponse HandleOperations(const HttpRequest& request) {
        if (dependencies_.logger_service == nullptr) {
            return StatusResponse(501, "Not Implemented");
        }
        AuthPrincipal principal;
        infra::Status permission = RequirePermission(
            request, AuthPermission::kManageUsers, "operations", &principal);
        if (permission != infra::Status::kOk) {
            return StatusToResponse(permission);
        }
        OperationLogQuery query;
        query.limit = 100;
        infra::Result<std::vector<OperationRecord>> records =
            dependencies_.logger_service->QueryOperations(query);
        if (!records.IsOk()) {
            return StatusToResponse(records.status);
        }
        Json root = Json::object();
        Json items = Json::array();
        for (const OperationRecord& record : records.value) {
            items.push_back(OperationRecordToJson(record));
        }
        root["items"] = items;
        return JsonResponse(200, root);
    }

    HttpResponse HandleStaticFile(const HttpRequest& request) {
        if (options_.static_root.empty()) {
            IncrementNotFound();
            return StatusResponse(404, "Not Found");
        }
        if (IsUnsafeStaticPath(request.path)) {
            return StatusResponse(403, "Forbidden");
        }
        std::string relative = request.path == "/" ? "index.html" : request.path.substr(1);
        if (relative.empty()) {
            relative = "index.html";
        }
        const std::string path = infra::Path::Join(options_.static_root, relative);
        infra::Result<std::string> content = infra::File::ReadAll(path);
        if (!content.IsOk()) {
            IncrementNotFound();
            return StatusToResponse(content.status);
        }
        HttpResponse response;
        response.status_code = 200;
        response.headers["Content-Type"] = ContentTypeForPath(path);
        response.body = content.value;
        return response;
    }

    void OnConnection(ConnectionId connection_id, NetAddress peer) {
        NetEngine* net_engine = nullptr;
        {
            infra::MutexGuard guard(&mutex_);
            HttpSession session;
            session.client_ip = std::move(peer.ip);
            sessions_[connection_id] = session;
            ++stats_.active_connections;
            net_engine = dependencies_.net_engine;
        }
        if (net_engine != nullptr) {
            (void)net_engine->RunOnIoAfter(
                options_.request_timeout_ms, [this, connection_id]() {
                bool should_close = false;
                {
                    infra::MutexGuard guard(&mutex_);
                    auto iter = sessions_.find(connection_id);
                    should_close = iter != sessions_.end() &&
                                   !iter->second.request_posted;
                }
                if (should_close) {
                    NetEngine* engine = nullptr;
                    {
                        infra::MutexGuard guard(&mutex_);
                        engine = dependencies_.net_engine;
                    }
                    if (engine != nullptr) {
                        (void)engine->Close(connection_id);
                    }
                }
            });
        }
    }

    void OnClose(ConnectionId connection_id) {
        (void)connection_id;
        infra::MutexGuard guard(&mutex_);
        sessions_.erase(connection_id);
        if (stats_.active_connections > 0) {
            --stats_.active_connections;
        }
    }

    void OnMessage(ConnectionId connection_id, const uint8_t* data, uint32_t size) {
        if (data == nullptr) {
            return;
        }
        RawParseResult parsed;
        bool should_handle = false;
        bool should_close = false;
        {
            infra::MutexGuard guard(&mutex_);
            auto iter = sessions_.find(connection_id);
            if (iter == sessions_.end() || iter->second.request_posted) {
                return;
            }
            iter->second.recv_buffer.append(reinterpret_cast<const char*>(data), size);
            if (iter->second.recv_buffer.size() >
                static_cast<size_t>(options_.max_request_header_bytes) + 4 +
                    options_.max_request_body_bytes) {
                parsed.status = RawParseStatus::kPayloadTooLarge;
            } else {
                parsed = ParseRawRequest(iter->second.recv_buffer,
                                         options_.max_request_header_bytes,
                                         options_.max_request_body_bytes,
                                         iter->second.client_ip);
            }
            if (parsed.status == RawParseStatus::kIncomplete) {
                return;
            }
            iter->second.request_posted = true;
            should_handle = parsed.status == RawParseStatus::kComplete;
            should_close = !should_handle;
        }

        if (should_close) {
            IncrementParseFailures();
            const HttpResponse response =
                parsed.status == RawParseStatus::kPayloadTooLarge
                    ? StatusResponse(413, "Payload Too Large")
                    : StatusResponse(400, "Bad Request");
            SendResponseAndClose(connection_id, response);
            return;
        }

        infra::Executor* task_executor = nullptr;
        {
            infra::MutexGuard guard(&mutex_);
            task_executor = task_executor_.get();
        }
        if (task_executor == nullptr ||
            task_executor->Post([this, connection_id, request = parsed.request]() {
                infra::Result<HttpResponse> handled = HandleRequest(request);
                const HttpResponse response =
                    handled.IsOk() ? handled.value : StatusToResponse(handled.status);
                SendResponseAndClose(connection_id, response);
            }) != infra::Status::kOk) {
            SendResponseAndClose(connection_id,
                                 StatusResponse(503, "Service Unavailable"));
        }
    }

    void SendResponseAndClose(ConnectionId connection_id, const HttpResponse& response) {
        NetEngine* net_engine = nullptr;
        {
            infra::MutexGuard guard(&mutex_);
            net_engine = dependencies_.net_engine;
        }
        if (net_engine == nullptr) {
            return;
        }
        const std::string serialized = SerializeResponse(response);
        infra::Status send_error = net_engine->Send(
            connection_id, reinterpret_cast<const uint8_t*>(serialized.data()),
            serialized.size());
        if (send_error == infra::Status::kOk) {
            (void)net_engine->CloseAfterSend(connection_id);
            return;
        }
        (void)net_engine->Close(connection_id);
    }

    struct HttpSession {
        std::string recv_buffer;
        std::string client_ip;
        bool request_posted = false;
    };

    HttpServiceOptions options_;
    HttpServiceDependencies dependencies_;
    mutable infra::Mutex mutex_;
    std::unique_ptr<infra::Executor> task_executor_;
    TcpServerId tcp_server_id_ = 0;
    std::map<ConnectionId, HttpSession> sessions_;
    HttpServiceStats stats_;
    uint64_t next_request_id_ = 0;
    bool initialized_ = false;
    bool started_ = false;
};

std::unique_ptr<IHttpService> CreateHttpService(
    const HttpServiceOptions& options,
    const HttpServiceDependencies& dependencies) {
    return std::unique_ptr<IHttpService>(
        new HttpServiceImpl(options, dependencies));
}

}  // namespace live_stream
