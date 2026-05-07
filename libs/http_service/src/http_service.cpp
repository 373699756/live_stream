#include "http_service.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "ai_service.h"
#include "auth_service.h"
#include "config_service.h"
#include "http_protocol.h"
#include "infra/executor.h"
#include "infra/fs.h"
#include "infra/time.h"
#include "live_stream/json_utils.h"
#include "logger_service.h"
#include "media_service.h"
#include "netframe_service.h"
#include "network_service.h"
#include "rtsp_service.h"
#include "snapshot_service.h"
#include "system_service.h"
#include "time_service.h"
#include "upgrade_service.h"
#include "webrtc_service.h"

namespace live_stream {
namespace {

constexpr const char *kModuleName = "http_service";

HttpResponse JsonResponse(int status_code, const ConfigJson &value) {
  HttpResponse response;
  response.status_code = status_code;
  response.headers["Content-Type"] = "application/json";
  response.body = value.dump();
  return response;
}

HttpResponse StatusResponse(int status_code, const std::string &reason) {
  ConfigJson root = ConfigJson::object();
  root["error"] = reason;
  return JsonResponse(status_code, root);
}

HttpResponse OkResponse() {
  ConfigJson root = ConfigJson::object();
  root["ok"] = true;
  return JsonResponse(200, root);
}

std::string RequestUserAgent(const HttpRequest &request) {
  return GetHeader(request, "User-Agent");
}

std::string MakeRequestId(uint64_t id) {
  return std::string("http-") +
         std::to_string(infra::Time::SystemTimeMillis()) + "-" +
         std::to_string(id);
}

std::string ExtractBearerToken(const HttpRequest &request) {
  const std::string authorization = Trim(GetHeader(request, "Authorization"));
  const std::string prefix = "bearer ";
  if (authorization.size() > prefix.size() &&
      ToLower(authorization.substr(0, prefix.size())) == prefix) {
    return Trim(authorization.substr(prefix.size()));
  }
  const std::string key = "token=";
  size_t pos = request.query_string.find(key);
  if (pos == std::string::npos) {
    return std::string();
  }
  pos += key.size();
  const size_t end = request.query_string.find('&', pos);
  return request.query_string.substr(
      pos, end == std::string::npos ? std::string::npos : end - pos);
}

std::string AuthRoleToJsonString(AuthRole role) {
  return AuthRoleToString(role);
}

const char *StreamIdToJsonString(StreamId stream_id) {
  switch (stream_id) {
  case StreamId::kMain:
    return "main";
  case StreamId::kSub:
    return "sub";
  case StreamId::kSnapshot:
    return "snapshot";
  }
  return "unknown";
}

bool StreamIdFromJsonString(const std::string &value, StreamId *stream_id) {
  if (stream_id == nullptr) {
    return false;
  }
  if (value == "main") {
    *stream_id = StreamId::kMain;
    return true;
  }
  if (value == "sub") {
    *stream_id = StreamId::kSub;
    return true;
  }
  return false;
}

const char *VideoCodecToJsonString(VideoCodec codec) {
  switch (codec) {
  case VideoCodec::kH264:
    return "h264";
  case VideoCodec::kH265:
    return "h265";
  case VideoCodec::kJpeg:
    return "jpeg";
  case VideoCodec::kMjpeg:
    return "mjpeg";
  }
  return "unknown";
}

const char *RateControlModeToJsonString(RateControlMode mode) {
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

ConfigJson VideoResolutionToJson(const VideoResolution &resolution) {
  ConfigJson root = ConfigJson::object();
  root["width"] = resolution.width;
  root["height"] = resolution.height;
  return root;
}

ConfigJson CodecCapabilityToJson(const CodecCapability &capability) {
  ConfigJson root = ConfigJson::object();
  root["codec"] = VideoCodecToJsonString(capability.codec);
  ConfigJson profiles = ConfigJson::array();
  for (const std::string &profile : capability.profiles) {
    profiles.push_back(profile);
  }
  root["profiles"] = profiles;
  return root;
}

ConfigJson StreamCapabilitiesToJson(const VideoStreamCapabilities &stream,
                                    bool available) {
  ConfigJson root = ConfigJson::object();
  root["stream"] = StreamIdToJsonString(stream.stream_id);
  root["available"] = available;

  ConfigJson codecs = ConfigJson::array();
  for (const CodecCapability &capability : stream.codecs) {
    codecs.push_back(CodecCapabilityToJson(capability));
  }
  root["codecs"] = codecs;

  ConfigJson resolutions = ConfigJson::array();
  for (const VideoResolution &resolution : stream.resolutions) {
    resolutions.push_back(VideoResolutionToJson(resolution));
  }
  root["resolutions"] = resolutions;

  ConfigJson fps = ConfigJson::object();
  fps["min"] = stream.frame_rate.min_fps;
  fps["max"] = stream.frame_rate.max_fps;
  root["fps"] = fps;

  ConfigJson bitrate = ConfigJson::object();
  bitrate["min"] = stream.bitrate.min_kbps;
  bitrate["max"] = stream.bitrate.max_kbps;
  root["bitrate_kbps"] = bitrate;

  ConfigJson rate_control = ConfigJson::array();
  for (RateControlMode mode : stream.rate_control_modes) {
    rate_control.push_back(RateControlModeToJsonString(mode));
  }
  root["rate_control"] = rate_control;

  ConfigJson gop = ConfigJson::object();
  gop["min"] = stream.gop.min;
  gop["max"] = stream.gop.max;
  root["gop"] = gop;
  root["smart_codec"] = stream.smart_codec_supported;
  return root;
}

ConfigJson
NumericControlsToJson(const std::vector<NumericControlCapability> &controls) {
  ConfigJson root = ConfigJson::object();
  for (const NumericControlCapability &control : controls) {
    ConfigJson value = ConfigJson::object();
    value["min"] = control.min;
    value["max"] = control.max;
    value["default"] = control.default_value;
    root[control.name] = value;
  }
  return root;
}

ConfigJson
OptionControlsToJson(const std::vector<OptionControlCapability> &controls) {
  ConfigJson root = ConfigJson::object();
  for (const OptionControlCapability &control : controls) {
    ConfigJson values = ConfigJson::array();
    for (const std::string &value : control.values) {
      values.push_back(value);
    }
    ConfigJson item = ConfigJson::object();
    item["values"] = values;
    item["default"] = control.default_value;
    root[control.name] = item;
  }
  return root;
}

ConfigJson ImageCapabilitiesToJson(const ImageCapabilities &image) {
  ConfigJson root = ConfigJson::object();
  root["basic"] = NumericControlsToJson(image.basic);

  ConfigJson exposure = ConfigJson::object();
  exposure["options"] = OptionControlsToJson(image.exposure_options);
  exposure["ranges"] = NumericControlsToJson(image.exposure_ranges);
  root["exposure"] = exposure;

  ConfigJson white_balance = ConfigJson::object();
  white_balance["options"] = OptionControlsToJson(image.white_balance_options);
  white_balance["ranges"] = NumericControlsToJson(image.white_balance_ranges);
  root["white_balance"] = white_balance;

  ConfigJson enhancement = ConfigJson::object();
  enhancement["options"] = OptionControlsToJson(image.enhancement_options);
  enhancement["ranges"] = NumericControlsToJson(image.enhancement_ranges);
  root["enhancement"] = enhancement;

  ConfigJson backlight = ConfigJson::object();
  backlight["options"] = OptionControlsToJson(image.backlight_options);
  backlight["ranges"] = NumericControlsToJson(image.backlight_ranges);
  root["backlight"] = backlight;

  root["color_mode"] = OptionControlsToJson(image.color_mode_options);
  root["orientation"]["mirror"] = image.mirror_supported;
  root["orientation"]["flip"] = image.flip_supported;
  return root;
}

ConfigJson MediaCapabilitiesToJson(const MediaCapabilities &capabilities,
                                   const MediaService *media_service) {
  ConfigJson root = ConfigJson::object();
  ConfigJson streams = ConfigJson::object();
  for (const VideoStreamCapabilities &stream : capabilities.streams) {
    const char *name = StreamIdToJsonString(stream.stream_id);
    if (std::strcmp(name, "unknown") != 0) {
      const bool available = media_service == nullptr ||
                             media_service->IsStreamSupported(stream.stream_id);
      streams[name] = StreamCapabilitiesToJson(stream, available);
    }
  }
  root["streams"] = streams;
  root["image"] = ImageCapabilitiesToJson(capabilities.image);
  return root;
}

const char *AiBackendToJsonString(AiBackend backend) {
  switch (backend) {
  case AiBackend::kHi3516Dv300Nnie:
    return "hisi3516dv300_nnie";
  case AiBackend::kHostStub:
    return "host_stub";
  }
  return "unknown";
}

const char *AiTaskToJsonString(AiTask task) {
  switch (task) {
  case AiTask::kObjectDetection:
    return "object_detection";
  case AiTask::kFaceDetection:
    return "face_detection";
  case AiTask::kMotionClassification:
    return "motion_classification";
  }
  return "unknown";
}

ConfigJson AiConfigToJson(const AiModelConfig &config) {
  ConfigJson root = ConfigJson::object();
  root["enabled"] = config.enabled;
  root["backend"] = AiBackendToJsonString(config.backend);
  root["task"] = AiTaskToJsonString(config.task);
  root["stream"] = StreamIdToJsonString(config.stream_id);
  root["model_path"] = config.model_path;
  root["input_width"] = config.input_width;
  root["input_height"] = config.input_height;
  root["inference_interval_ms"] = config.inference_interval_ms;
  root["confidence_threshold"] = config.confidence_threshold;
  root["max_results"] = config.max_results;
  return root;
}

ConfigJson AiStatsToJson(const AiServiceStats &stats) {
  ConfigJson root = ConfigJson::object();
  root["enabled"] = stats.enabled;
  root["backend_available"] = stats.backend_available;
  root["received_frames"] = stats.received_frames;
  root["skipped_frames"] = stats.skipped_frames;
  root["inference_count"] = stats.inference_count;
  root["inference_failed_count"] = stats.inference_failed_count;
  root["dropped_tasks"] = stats.dropped_tasks;
  root["active_results"] = stats.active_results;
  return root;
}

ConfigJson AiDetectionToJson(const AiDetection &detection) {
  ConfigJson root = ConfigJson::object();
  root["label"] = detection.label;
  root["confidence"] = detection.confidence;
  root["x"] = detection.x;
  root["y"] = detection.y;
  root["width"] = detection.width;
  root["height"] = detection.height;
  return root;
}

ConfigJson AiResultToJson(const AiInferenceResult &result) {
  ConfigJson root = ConfigJson::object();
  root["success"] = result.success;
  root["stream"] = StreamIdToJsonString(result.stream_id);
  root["sequence"] = result.sequence;
  root["pts_us"] = result.pts_us;
  ConfigJson detections = ConfigJson::array();
  for (const AiDetection &detection : result.detections) {
    detections.push_back(AiDetectionToJson(detection));
  }
  root["detections"] = detections;
  return root;
}

bool IsAiServiceHealthy(const AiService *service) {
  if (service == nullptr) {
    return false;
  }
  const AiServiceStats stats = service->GetStats();
  return !stats.enabled || stats.backend_available;
}

bool IsAiConfigEnabled(IConfigService *config_service) {
  if (config_service == nullptr) {
    return false;
  }
  ConfigJson config = config_service->GetValue("ai");
  bool enabled = false;
  return config.is_object() && json_utils::Load(config, "enabled", &enabled) &&
         enabled;
}

ConfigJson PrincipalToJson(const AuthPrincipal &principal) {
  ConfigJson root = ConfigJson::object();
  root["user_name"] = principal.user_name;
  root["session_id"] = principal.session_id;
  root["role"] = AuthRoleToJsonString(principal.role);
  return root;
}

ConfigJson OperationRecordToJson(const OperationRecord &record) {
  ConfigJson root = ConfigJson::object();
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

std::string UptimeToString(int64_t uptime_ms) {
  if (uptime_ms <= 0) {
    return "0s";
  }
  const int64_t total_seconds = uptime_ms / 1000;
  const int64_t days = total_seconds / (24 * 60 * 60);
  const int64_t hours = (total_seconds / (60 * 60)) % 24;
  const int64_t minutes = (total_seconds / 60) % 60;
  const int64_t seconds = total_seconds % 60;
  if (days > 0) {
    return std::to_string(days) + "d " + std::to_string(hours) + "h " +
           std::to_string(minutes) + "m";
  }
  if (hours > 0) {
    return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
  }
  if (minutes > 0) {
    return std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
  }
  return std::to_string(seconds) + "s";
}

ConfigJson SystemCapabilitiesToJson(const SystemCapabilities &capabilities) {
  ConfigJson root = ConfigJson::object();
  root["supports_reboot"] = capabilities.supports_reboot;
  root["supports_factory_reset"] = capabilities.supports_factory_reset;
  ConfigJson features = ConfigJson::array();
  for (const std::string &feature : capabilities.features) {
    features.push_back(feature);
  }
  root["features"] = features;
  return root;
}

ConfigJson NtpConfigToJson(const NtpConfig &config) {
  ConfigJson root = ConfigJson::object();
  root["enabled"] = config.enabled;
  ConfigJson servers = ConfigJson::array();
  for (const std::string &server : config.servers) {
    servers.push_back(server);
  }
  root["servers"] = servers;
  root["sync_interval_sec"] = config.sync_interval_sec;
  return root;
}

bool NtpConfigFromJson(const ConfigJson &value, NtpConfig *config) {
  if (config == nullptr || !value.is_object()) {
    return false;
  }
  NtpConfig parsed;
  if (!json_utils::Load(value, "enabled", &parsed.enabled) ||
      !json_utils::LoadStringArray(value, "servers", &parsed.servers) ||
      !json_utils::Load(value, "sync_interval_sec", &parsed.sync_interval_sec,
                        1, 0xffffffffU)) {
    return false;
  }
  *config = parsed;
  return true;
}

ConfigJson TimeStatusToJson(const TimeStatus &status) {
  ConfigJson root = ConfigJson::object();
  root["system_time_ms"] = status.system_time_ms;
  root["timezone"] = status.timezone;
  root["ntp"] = NtpConfigToJson(status.ntp);
  root["last_sync_source"] = TimeSyncSourceToString(status.last_sync_source);
  root["last_sync_time_ms"] = status.last_sync_time_ms;
  root["last_sync_ok"] = status.last_sync_ok;
  return root;
}

std::string NetmaskFromPrefix(uint8_t prefix_length) {
  if (prefix_length == 0 || prefix_length > 32) {
    return std::string();
  }
  const uint32_t mask =
      prefix_length == 32 ? 0xffffffffu : (0xffffffffu << (32 - prefix_length));
  return std::to_string((mask >> 24) & 0xff) + "." +
         std::to_string((mask >> 16) & 0xff) + "." +
         std::to_string((mask >> 8) & 0xff) + "." + std::to_string(mask & 0xff);
}

ConfigJson NetworkInterfaceStatusToJson(const NetworkInterfaceStatus &status) {
  ConfigJson root = ConfigJson::object();
  root["ifname"] = status.ifname;
  root["enabled"] = status.enabled;
  root["link_up"] = status.link_up;
  root["dhcp"] = status.dhcp_enabled;
  root["mac_address"] = status.mac_address;
  root["gateway"] = status.gateway;
  root["last_ok"] = status.last_ok;
  ConfigJson static_ipv4 = ConfigJson::object();
  static_ipv4["address"] = status.ipv4_address;
  static_ipv4["prefix_length"] = status.prefix_length;
  static_ipv4["netmask"] = NetmaskFromPrefix(status.prefix_length);
  static_ipv4["gateway"] = status.gateway;
  root["static_ipv4"] = static_ipv4;
  ConfigJson dns = ConfigJson::array();
  for (const std::string &server : status.dns_servers) {
    dns.push_back(server);
  }
  root["dns"] = dns;
  return root;
}

bool PrefixFromNetmask(const std::string &netmask, uint8_t *prefix_length) {
  if (prefix_length == nullptr) {
    return false;
  }
  struct in_addr address;
  if (inet_pton(AF_INET, netmask.c_str(), &address) != 1) {
    return false;
  }
  uint32_t mask = ntohl(address.s_addr);
  uint8_t bits = 0;
  while ((mask & 0x80000000u) != 0) {
    ++bits;
    mask <<= 1;
  }
  if (mask != 0) {
    return false;
  }
  *prefix_length = bits;
  return bits > 0;
}

bool NetworkInterfaceConfigFromJson(const std::string &ifname,
                                    const ConfigJson &value,
                                    NetworkInterfaceConfig *config) {
  if (config == nullptr || !value.is_object()) {
    return false;
  }
  const ConfigJson *static_ipv4 = nullptr;
  NetworkInterfaceConfig parsed;
  parsed.ifname = ifname;
  bool dhcp = false;
  std::string netmask;
  if (!json_utils::Load(value, "enabled", &parsed.enabled) ||
      !json_utils::Load(value, "dhcp", &dhcp) ||
      !json_utils::LoadStringArray(value, "dns", &parsed.dns_servers) ||
      !json_utils::LoadObject(value, "static_ipv4", &static_ipv4) ||
      !json_utils::Load(*static_ipv4, "address", &parsed.ipv4_address) ||
      !json_utils::Load(*static_ipv4, "gateway", &parsed.gateway) ||
      !json_utils::Load(*static_ipv4, "netmask", &netmask) ||
      !PrefixFromNetmask(netmask, &parsed.prefix_length)) {
    return false;
  }
  parsed.address_mode =
      dhcp ? NetworkAddressMode::kDhcp : NetworkAddressMode::kStatic;
  *config = parsed;
  return true;
}

ConfigJson UpgradePackageInfoToJson(const UpgradePackageInfo &info) {
  ConfigJson root = ConfigJson::object();
  root["package_path"] = info.package_path;
  root["version"] = info.version;
  root["size_bytes"] = info.size_bytes;
  root["digest"] = info.digest;
  root["build_time_ms"] = info.build_time_ms;
  root["target_model"] = info.target_model;
  root["requires_reboot"] = info.requires_reboot;
  return root;
}

ConfigJson UpgradeStatusToJson(const UpgradeStatus &status) {
  ConfigJson root = ConfigJson::object();
  root["state"] = UpgradeStateToString(status.state);
  root["progress_percent"] = status.progress_percent;
  root["current_stage"] = status.current_stage;
  root["target_version"] = status.target_version;
  root["ok"] = status.ok;
  root["error_message"] = status.error_message;
  root["started_at_ms"] = status.started_at_ms;
  root["finished_at_ms"] = status.finished_at_ms;
  return root;
}

bool UpgradeRequestFromJson(const ConfigJson &value, UpgradeRequest *request) {
  if (request == nullptr || !value.is_object()) {
    return false;
  }
  UpgradeRequest parsed;
  if (!json_utils::Load(value, "package_path", &parsed.package_path) ||
      !json_utils::Load(value, "expected_version", &parsed.expected_version) ||
      !json_utils::Load(value, "allow_same_version",
                        &parsed.allow_same_version) ||
      !json_utils::Load(value, "allow_downgrade", &parsed.allow_downgrade) ||
      !json_utils::Load(value, "auto_reboot", &parsed.auto_reboot)) {
    return false;
  }
  *request = parsed;
  return true;
}

std::string ContentTypeForPath(const std::string &path) {
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

bool IsUnsafeStaticPath(const std::string &path) {
  const std::string lower_path = ToLower(path);
  return path.find("..") != std::string::npos ||
         path.find('\\') != std::string::npos ||
         lower_path.find("%2e") != std::string::npos ||
         lower_path.find("%5c") != std::string::npos;
}

} // namespace

class HttpServiceImpl : public IHttpService {
public:
  HttpServiceImpl(const HttpServiceOptions &options,
                  const HttpServiceDependencies &dependencies)
      : options_(options), dependencies_(dependencies) {}

  ~HttpServiceImpl() override {
    Stop();
    Release();
  }

  bool Prepare() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (initialized_) {
      return true;
    }
    if (dependencies_.auth_service == nullptr ||
        dependencies_.config_service == nullptr) {
      return false;
    }
    if (options_.max_request_header_bytes == 0 ||
        options_.max_request_body_bytes == 0 || options_.max_connections == 0 ||
        options_.request_timeout_ms == 0 ||
        options_.connection_idle_timeout_ms == 0 ||
        options_.max_requests_per_connection == 0 ||
        options_.max_pipelined_requests == 0 ||
        options_.executor_worker_count == 0 ||
        options_.executor_queue_capacity == 0) {
      return false;
    }
    task_executor_.reset(new infra::Executor());
    initialized_ = true;
    return true;
  }

  bool Start() override {
    if (!Prepare()) {
      return false;
    }
    infra::Executor *task_executor = nullptr;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (started_) {
        return true;
      }
      if (dependencies_.net_engine == nullptr) {
        return false;
      }
      task_executor = task_executor_.get();
    }
    infra::ExecutorOptions executor_options;
    executor_options.worker_count = options_.executor_worker_count;
    executor_options.queue_capacity = options_.executor_queue_capacity;
    if (!task_executor->Start(executor_options)) {
      return false;
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
    TcpServerId server =
        dependencies_.net_engine->ListenTcp(server_config, callbacks);
    if (server == 0) {
      task_executor->Stop(infra::StopMode::kDiscard);
      return false;
    }
    {
      std::lock_guard<std::mutex> guard(mutex_);
      tcp_server_id_ = server;
      started_ = true;
    }
    return true;
  }

  void Stop() override {
    TcpServerId server_id = 0;
    NetEngine *net_engine = nullptr;
    infra::Executor *task_executor = nullptr;
    {
      std::lock_guard<std::mutex> guard(mutex_);
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

  void Release() {
    Stop();
    std::lock_guard<std::mutex> guard(mutex_);
    sessions_.clear();
    task_executor_.reset();
    initialized_ = false;
  }

  HttpResponse HandleRequest(const HttpRequest &request) override {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (!initialized_) {
        return StatusResponse(500, "Service not initialized");
      }
      ++stats_.total_requests;
    }

    if (request.path.empty() || request.path[0] != '/') {
      IncrementParseFailures();
      return StatusResponse(400, "Invalid request path");
    }

    if (request.path == "/api/auth/login" &&
        request.method == HttpMethod::kPost) {
      return HandleLogin(request);
    }
    if (request.path == "/api/auth/logout" &&
        request.method == HttpMethod::kPost) {
      return HandleLogout(request);
    }
    if (request.path == "/api/auth/me" && request.method == HttpMethod::kGet) {
      return HandleMe(request);
    }
    if (request.path == "/api/media/capabilities" &&
        request.method == HttpMethod::kGet) {
      return HandleMediaCapabilities();
    }
    if (request.path == "/api/status/streams" &&
        request.method == HttpMethod::kGet) {
      return HandleStreamStatus(request);
    }
    if (request.path == "/api/system/status" &&
        request.method == HttpMethod::kGet) {
      return HandleSystemStatus(request);
    }
    if (request.path == "/api/system/capabilities" &&
        request.method == HttpMethod::kGet) {
      return HandleSystemCapabilities(request);
    }
    if (request.path == "/api/system/reboot" &&
        request.method == HttpMethod::kPost) {
      return HandleSystemReboot(request);
    }
    if (request.path == "/api/system/factory-reset" &&
        request.method == HttpMethod::kPost) {
      return HandleSystemFactoryReset(request);
    }
    if (request.path == "/api/time/status" &&
        request.method == HttpMethod::kGet) {
      return HandleTimeStatus(request);
    }
    if (request.path == "/api/time/timezone" &&
        request.method == HttpMethod::kPut) {
      return HandleTimeTimezone(request);
    }
    if (request.path == "/api/time/ntp" && request.method == HttpMethod::kPut) {
      return HandleTimeNtp(request);
    }
    if (request.path == "/api/time/system-time" &&
        request.method == HttpMethod::kPost) {
      return HandleTimeSystemTime(request);
    }
    if (request.path == "/api/time/sync" &&
        request.method == HttpMethod::kPost) {
      return HandleTimeSync(request);
    }
    if (request.path == "/api/network/interfaces" &&
        request.method == HttpMethod::kGet) {
      return HandleNetworkInterfaces(request);
    }
    if (StartsWith(request.path, "/api/network/interfaces/") &&
        (request.method == HttpMethod::kGet ||
         request.method == HttpMethod::kPut)) {
      return HandleNetworkInterface(request);
    }
    if (request.path == "/api/network/reload" &&
        request.method == HttpMethod::kPost) {
      return HandleNetworkReload(request);
    }
    if (request.path == "/api/upgrade/status" &&
        request.method == HttpMethod::kGet) {
      return HandleUpgradeStatus(request);
    }
    if (request.path == "/api/upgrade/validate" &&
        request.method == HttpMethod::kPost) {
      return HandleUpgradeValidate(request);
    }
    if (request.path == "/api/upgrade/start" &&
        request.method == HttpMethod::kPost) {
      return HandleUpgradeStart(request);
    }
    if (request.path == "/api/upgrade/cancel" &&
        request.method == HttpMethod::kPost) {
      return HandleUpgradeCancel(request);
    }
    if (request.path == "/api/upgrade/confirm-reboot" &&
        request.method == HttpMethod::kPost) {
      return HandleUpgradeConfirmReboot(request);
    }
    if (request.path == "/api/ai/status" &&
        request.method == HttpMethod::kGet) {
      return HandleAiStatus(request);
    }
    if (StartsWith(request.path, "/api/snapshot/") &&
        request.method == HttpMethod::kGet) {
      return HandleSnapshot(request);
    }
    if (StartsWith(request.path, "/api/webrtc") &&
        (request.method == HttpMethod::kPost ||
         request.method == HttpMethod::kDelete)) {
      return HandleWebrtc(request);
    }
    if (StartsWith(request.path, "/api/config/")) {
      return HandleConfig(request);
    }
    if (request.path == "/api/operations/export" &&
        request.method == HttpMethod::kGet) {
      return HandleOperationsExport(request);
    }
    if (request.path == "/api/operations" &&
        request.method == HttpMethod::kGet) {
      return HandleOperations(request);
    }
    if (StartsWith(request.path, "/api/")) {
      return StatusResponse(501, "Not Implemented");
    }
    if (request.method == HttpMethod::kGet && options_.enable_static_files) {
      return HandleStaticFile(request);
    }

    IncrementNotFound();
    return StatusResponse(404, "Not Found");
  }

  HttpServiceStats GetStats() const override {
    std::lock_guard<std::mutex> guard(mutex_);
    return stats_;
  }

  HttpListenAddress LocalAddress() const override {
    std::lock_guard<std::mutex> guard(mutex_);
    if (dependencies_.net_engine == nullptr || tcp_server_id_ == 0) {
      return HttpListenAddress{};
    }
    NetAddress address =
        dependencies_.net_engine->TcpLocalAddress(tcp_server_id_);
    HttpListenAddress result;
    result.ip = address.ip;
    result.port = address.port;
    return result;
  }

private:
  struct PendingRequest {
    HttpRequest request;
    bool close_after_response = true;
  };

  struct HttpSession {
    std::string recv_buffer;
    std::string client_ip;
    std::deque<PendingRequest> pending_requests;
    uint64_t request_count = 0;
    uint64_t timeout_generation = 0;
    bool processing = false;
    bool closing = false;
  };

  static void HandleAccept(void *user, ConnectionId id, NetAddress peer) {
    HttpServiceImpl *self = static_cast<HttpServiceImpl *>(user);
    if (self != nullptr) {
      self->OnConnection(id, std::move(peer));
    }
  }

  static void HandleRead(void *user, ConnectionId id, const uint8_t *data,
                         size_t size) {
    HttpServiceImpl *self = static_cast<HttpServiceImpl *>(user);
    if (self != nullptr) {
      self->OnMessage(id, data, static_cast<uint32_t>(size));
    }
  }

  static void HandleClose(void *user, ConnectionId id) {
    HttpServiceImpl *self = static_cast<HttpServiceImpl *>(user);
    if (self != nullptr) {
      self->OnClose(id);
    }
  }

  static bool StartsWith(const std::string &value, const std::string &prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
  }

  void IncrementParseFailures() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.parse_failures;
  }

  void IncrementNotFound() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.not_found;
  }

  void IncrementAuthFailures() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.auth_failures;
  }

  void IncrementPermissionDenied() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.permission_denied;
  }

  live_stream::RequestContext MakeContext(const HttpRequest &request,
                                          const AuthPrincipal *principal) {
    live_stream::RequestContext context;
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
    std::lock_guard<std::mutex> guard(mutex_);
    return ++next_request_id_;
  }

  AuthPrincipal Authenticate(const HttpRequest &request) {
    const std::string token = ExtractBearerToken(request);
    if (token.empty()) {
      IncrementAuthFailures();
      return AuthPrincipal{};
    }
    TokenValidationResult validated =
        dependencies_.auth_service->ValidateToken(token);
    if (validated.principal.user_name.empty()) {
      IncrementAuthFailures();
      return AuthPrincipal{};
    }
    return validated.principal;
  }

  bool RequirePermission(const HttpRequest &request, AuthPermission permission,
                         const std::string &target, AuthPrincipal *principal) {
    AuthPrincipal authenticated = Authenticate(request);
    if (authenticated.user_name.empty()) {
      return false;
    }
    if (!dependencies_.auth_service->CheckPermission(authenticated, permission,
                                                     target)) {
      IncrementPermissionDenied();
      RecordOperation(request, authenticated,
                      OperationAction::kPermissionDenied, target,
                      OperationResult::kRejected, "permission_denied");
      return false;
    }
    if (principal != nullptr) {
      *principal = authenticated;
    }
    return true;
  }

  void RecordOperation(const HttpRequest &request,
                       const AuthPrincipal &principal, OperationAction action,
                       const std::string &target, OperationResult result,
                       const std::string &reason) {
    if (dependencies_.logger_service == nullptr) {
      return;
    }
    live_stream::RequestContext context = MakeContext(request, &principal);
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

  HttpResponse HandleLogin(const HttpRequest &request) {
    ConfigJson parsed = ConfigJson::parse(request.body, nullptr, false);
    if (parsed.is_discarded()) {
      IncrementParseFailures();
      return StatusResponse(400, "Invalid JSON");
    }
    std::string user_name;
    std::string password;
    if (!json_utils::Load(parsed, "user_name", &user_name) ||
        !json_utils::Load(parsed, "password", &password)) {
      return StatusResponse(400, "Invalid login request");
    }

    LoginRequest login_request;
    login_request.context = MakeContext(request, nullptr);
    login_request.user_name = user_name;
    login_request.password = password;
    LoginResult login = dependencies_.auth_service->Login(login_request);
    if (login.token.empty()) {
      IncrementAuthFailures();
      return StatusResponse(401, "Unauthorized");
    }

    ConfigJson root = ConfigJson::object();
    root["token"] = login.token;
    root["expires_at_ms"] = login.expires_at_ms;
    root["principal"] = PrincipalToJson(login.principal);
    return JsonResponse(200, root);
  }

  HttpResponse HandleLogout(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
      return StatusResponse(401, "Unauthorized");
    }
    live_stream::RequestContext context = MakeContext(request, &principal);
    if (!dependencies_.auth_service->Logout(context)) {
      return StatusResponse(404, "Not Found");
    }
    return OkResponse();
  }

  HttpResponse HandleMe(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
      return StatusResponse(401, "Unauthorized");
    }
    return JsonResponse(200, PrincipalToJson(principal));
  }

  HttpResponse HandleMediaCapabilities() {
    if (dependencies_.media_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    MediaCapabilities capabilities =
        dependencies_.media_service->GetCapabilities();
    if (capabilities.streams.empty()) {
      return StatusResponse(500, "Media capabilities unavailable");
    }
    return JsonResponse(200, MediaCapabilitiesToJson(
                                 capabilities, dependencies_.media_service));
  }

  HttpResponse HandleStreamStatus(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
      return StatusResponse(401, "Unauthorized");
    }
    if (dependencies_.media_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }

    ConfigJson items = ConfigJson::array();
    ConfigJson video_config = dependencies_.config_service->GetValue("video");
    if (!video_config.is_object() || !video_config.contains("streams") ||
        !video_config["streams"].is_object()) {
      return StatusResponse(500, "Invalid video config");
    }
    const ConfigJson &streams = video_config["streams"];
    const char *names[] = {"main", "sub"};
    for (const char *name : names) {
      ConfigJson item = ConfigJson::object();
      item["stream"] = name;
      if (!streams.contains(name) || !streams.at(name).is_object()) {
        return StatusResponse(500, "Invalid video config");
      }
      const ConfigJson &stream = streams.at(name);
      std::string codec;
      std::string resolution;
      int64_t fps = 0;
      int64_t bitrate_kbps = 0;
      bool stream_enabled = false;
      if (!json_utils::Load(stream, "codec", &codec) ||
          !json_utils::Load(stream, "resolution", &resolution) ||
          !json_utils::Load(stream, "fps", &fps, 1,
                            std::numeric_limits<int64_t>::max()) ||
          !json_utils::Load(stream, "bitrate_kbps", &bitrate_kbps, 1,
                            std::numeric_limits<int64_t>::max()) ||
          !json_utils::Load(stream, "enabled", &stream_enabled)) {
        return StatusResponse(500, "Invalid video config");
      }
      item["codec"] = codec;
      item["resolution"] = resolution;
      item["fps"] = fps;
      item["bitrateKbps"] = bitrate_kbps;
      StreamId stream_id = StreamId::kMain;
      (void)StreamIdFromJsonString(name, &stream_id);
      const bool stream_running =
          dependencies_.media_service->IsStreamStarted(stream_id);
      item["state"] = stream_running && stream_enabled ? "running" : "stopped";
      items.push_back(item);
    }
    return JsonResponse(200, items);
  }

  HttpResponse HandleSystemStatus(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
      return StatusResponse(401, "Unauthorized");
    }

    ConfigJson root = ConfigJson::object();
    DeviceInfo device_info;
    SystemStatus system_status;
    if (dependencies_.system_service != nullptr) {
      device_info = dependencies_.system_service->GetDeviceInfo();
      system_status = dependencies_.system_service->GetSystemStatus();
    }
    root["deviceName"] = device_info.serial_number.empty()
                             ? std::string("live-stream-ipc")
                             : device_info.serial_number;
    root["model"] = device_info.model.empty() ? std::string("live_stream_ipc")
                                              : device_info.model;
    root["firmware"] = device_info.firmware_version.empty()
                           ? std::string("0.1.0")
                           : device_info.firmware_version;
    root["uptime"] = UptimeToString(system_status.uptime_ms);
    root["cpu"] = system_status.cpu_usage_percent;
    root["memory"] = system_status.memory_usage_percent;
    root["temperature"] = system_status.temperature_celsius;
    ConfigJson services = ConfigJson::array();
    auto add_service = [&services](const char *name, bool running) {
      ConfigJson service = ConfigJson::object();
      service["name"] = name;
      service["state"] = running ? "running" : "pending";
      services.push_back(service);
    };
    add_service("logger_service", dependencies_.logger_service != nullptr);
    add_service("config_service", dependencies_.config_service != nullptr);
    add_service("auth_service", dependencies_.auth_service != nullptr);
    add_service("system_service", dependencies_.system_service != nullptr);
    add_service("time_service", dependencies_.time_service != nullptr);
    add_service("network_service", dependencies_.network_service != nullptr);
    add_service("alarm_service", dependencies_.alarm_service != nullptr);
    add_service("upgrade_service", dependencies_.upgrade_service != nullptr);
    add_service("rtsp_service",
                dependencies_.rtsp_service != nullptr &&
                    dependencies_.rtsp_service->LocalAddress().port != 0);
    add_service("onvif_service", dependencies_.onvif_service != nullptr);
    add_service("http_service", true);
    add_service("media_service", dependencies_.media_service != nullptr &&
                                     dependencies_.media_service->IsStarted());
    if (IsAiConfigEnabled(dependencies_.config_service)) {
      add_service("ai_service", IsAiServiceHealthy(dependencies_.ai_service));
    }
    add_service("snapshot_service",
                dependencies_.snapshot_service != nullptr &&
                    dependencies_.snapshot_service->GetStats().enabled);
    add_service("webrtc_service",
                dependencies_.webrtc_service != nullptr &&
                    dependencies_.webrtc_service->GetStats().enabled &&
                    dependencies_.webrtc_service->GetStats().backend_available);
    root["services"] = services;
    return JsonResponse(200, root);
  }

  HttpResponse HandleSystemCapabilities(const HttpRequest &request) {
    if (dependencies_.system_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kReadStatus, "system",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    return JsonResponse(200,
                        SystemCapabilitiesToJson(
                            dependencies_.system_service->GetCapabilities()));
  }

  HttpResponse HandleSystemReboot(const HttpRequest &request) {
    if (dependencies_.system_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kReboot, "system",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    const SystemCapabilities capabilities =
        dependencies_.system_service->GetCapabilities();
    if (!capabilities.supports_reboot) {
      return StatusResponse(501, "Reboot not supported");
    }
    return dependencies_.system_service->Reboot(
               MakeContext(request, &principal))
               ? OkResponse()
               : StatusResponse(503, "Reboot failed");
  }

  HttpResponse HandleSystemFactoryReset(const HttpRequest &request) {
    if (dependencies_.system_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kFactoryReset, "system",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    const SystemCapabilities capabilities =
        dependencies_.system_service->GetCapabilities();
    if (!capabilities.supports_factory_reset) {
      return StatusResponse(501, "Factory reset not supported");
    }
    return dependencies_.system_service->FactoryReset(
               MakeContext(request, &principal))
               ? OkResponse()
               : StatusResponse(503, "Factory reset failed");
  }

  HttpResponse HandleTimeStatus(const HttpRequest &request) {
    if (dependencies_.time_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kReadStatus, "time",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    return JsonResponse(
        200, TimeStatusToJson(dependencies_.time_service->GetTimeStatus()));
  }

  HttpResponse HandleTimeTimezone(const HttpRequest &request) {
    if (dependencies_.time_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, "time",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    ConfigJson body = ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
      return StatusResponse(400, "Invalid JSON");
    }
    std::string timezone;
    if (!json_utils::Load(body, "timezone", &timezone)) {
      return StatusResponse(400, "Invalid time request");
    }
    return dependencies_.time_service->SetTimezone(
               MakeContext(request, &principal), timezone)
               ? OkResponse()
               : StatusResponse(400, "Could not set timezone");
  }

  HttpResponse HandleTimeNtp(const HttpRequest &request) {
    if (dependencies_.time_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, "time",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    ConfigJson body = ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
      return StatusResponse(400, "Invalid JSON");
    }
    NtpConfig config;
    if (!NtpConfigFromJson(body, &config)) {
      return StatusResponse(400, "Invalid NTP config");
    }
    return dependencies_.time_service->UpdateNtpConfig(
               MakeContext(request, &principal), config)
               ? OkResponse()
               : StatusResponse(400, "Could not update NTP config");
  }

  HttpResponse HandleTimeSystemTime(const HttpRequest &request) {
    if (dependencies_.time_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, "time",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    ConfigJson body = ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
      return StatusResponse(400, "Invalid JSON");
    }
    int64_t system_time_ms = 0;
    if (!json_utils::Load(body, "system_time_ms", &system_time_ms, 1,
                          std::numeric_limits<int64_t>::max())) {
      return StatusResponse(400, "Invalid time request");
    }
    return dependencies_.time_service->SetSystemTime(
               MakeContext(request, &principal), system_time_ms,
               TimeSyncSource::kManual)
               ? OkResponse()
               : StatusResponse(503, "Could not set system time");
  }

  HttpResponse HandleTimeSync(const HttpRequest &request) {
    if (dependencies_.time_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, "time",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    return dependencies_.time_service->SyncNow(MakeContext(request, &principal),
                                               TimeSyncSource::kNtp)
               ? OkResponse()
               : StatusResponse(503, "Could not sync time");
  }

  HttpResponse HandleNetworkInterfaces(const HttpRequest &request) {
    if (dependencies_.network_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kReadStatus, "network",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    ConfigJson root = ConfigJson::object();
    ConfigJson items = ConfigJson::array();
    const std::vector<std::string> ifnames =
        dependencies_.network_service->GetInterfaces();
    for (const std::string &ifname : ifnames) {
      items.push_back(NetworkInterfaceStatusToJson(
          dependencies_.network_service->GetInterfaceStatus(ifname)));
    }
    root["items"] = items;
    return JsonResponse(200, root);
  }

  HttpResponse HandleNetworkInterface(const HttpRequest &request) {
    if (dependencies_.network_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    const std::string prefix = "/api/network/interfaces/";
    const std::string ifname = request.path.substr(prefix.size());
    if (ifname.empty()) {
      return StatusResponse(400, "Missing interface");
    }
    if (request.method == HttpMethod::kGet) {
      AuthPrincipal principal;
      if (!RequirePermission(request, AuthPermission::kReadStatus, ifname,
                             &principal)) {
        return StatusResponse(403, "Forbidden");
      }
      const NetworkInterfaceStatus status =
          dependencies_.network_service->GetInterfaceStatus(ifname);
      if (status.ifname.empty()) {
        return StatusResponse(404, "Not Found");
      }
      return JsonResponse(200, NetworkInterfaceStatusToJson(status));
    }

    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, ifname,
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    ConfigJson body = ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
      return StatusResponse(400, "Invalid JSON");
    }
    NetworkInterfaceConfig config;
    if (!NetworkInterfaceConfigFromJson(ifname, body, &config)) {
      return StatusResponse(400, "Invalid network config");
    }
    return dependencies_.network_service->ApplyInterfaceConfig(
               MakeContext(request, &principal), config)
               ? OkResponse()
               : StatusResponse(400, "Could not apply network config");
  }

  HttpResponse HandleNetworkReload(const HttpRequest &request) {
    if (dependencies_.network_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kModifyConfig, "network",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    return dependencies_.network_service->ReloadStatus()
               ? OkResponse()
               : StatusResponse(503, "Could not reload network status");
  }

  HttpResponse HandleUpgradeStatus(const HttpRequest &request) {
    if (dependencies_.upgrade_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kReadStatus, "upgrade",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    return JsonResponse(
        200, UpgradeStatusToJson(dependencies_.upgrade_service->GetStatus()));
  }

  HttpResponse HandleUpgradeValidate(const HttpRequest &request) {
    if (dependencies_.upgrade_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kUpgrade, "upgrade",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    ConfigJson body = ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
      return StatusResponse(400, "Invalid JSON");
    }
    std::string package_path;
    if (!json_utils::Load(body, "package_path", &package_path)) {
      return StatusResponse(400, "Invalid upgrade request");
    }
    const UpgradePackageInfo info =
        dependencies_.upgrade_service->ValidatePackage(package_path);
    if (info.version.empty()) {
      return StatusResponse(400, "Could not validate package");
    }
    return JsonResponse(200, UpgradePackageInfoToJson(info));
  }

  HttpResponse HandleUpgradeStart(const HttpRequest &request) {
    if (dependencies_.upgrade_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kUpgrade, "upgrade",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    ConfigJson body = ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
      return StatusResponse(400, "Invalid JSON");
    }
    UpgradeRequest upgrade_request;
    if (!UpgradeRequestFromJson(body, &upgrade_request)) {
      return StatusResponse(400, "Invalid upgrade request");
    }
    if (!dependencies_.upgrade_service->StartUpgrade(
            MakeContext(request, &principal), upgrade_request)) {
      return StatusResponse(409, "Could not start upgrade");
    }
    return JsonResponse(
        200, UpgradeStatusToJson(dependencies_.upgrade_service->GetStatus()));
  }

  HttpResponse HandleUpgradeCancel(const HttpRequest &request) {
    if (dependencies_.upgrade_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kUpgrade, "upgrade",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    if (!dependencies_.upgrade_service->CancelUpgrade(
            MakeContext(request, &principal))) {
      return StatusResponse(409, "Could not cancel upgrade");
    }
    return JsonResponse(
        200, UpgradeStatusToJson(dependencies_.upgrade_service->GetStatus()));
  }

  HttpResponse HandleUpgradeConfirmReboot(const HttpRequest &request) {
    if (dependencies_.upgrade_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kUpgrade, "upgrade",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    if (!dependencies_.upgrade_service->ConfirmReboot(
            MakeContext(request, &principal))) {
      return StatusResponse(409, "Could not confirm reboot");
    }
    return JsonResponse(
        200, UpgradeStatusToJson(dependencies_.upgrade_service->GetStatus()));
  }

  HttpResponse HandleAiStatus(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
      return StatusResponse(401, "Unauthorized");
    }
    if (dependencies_.ai_service == nullptr) {
      if (!IsAiConfigEnabled(dependencies_.config_service)) {
        ConfigJson root = ConfigJson::object();
        root["config"] = dependencies_.config_service->GetValue("ai");
        root["stats"] = AiStatsToJson(AiServiceStats{});
        root["last_result"] = AiResultToJson(AiInferenceResult{});
        return JsonResponse(200, root);
      }
      return StatusResponse(503, "AI service not running");
    }
    ConfigJson root = ConfigJson::object();
    root["config"] = AiConfigToJson(dependencies_.ai_service->GetConfig());
    root["stats"] = AiStatsToJson(dependencies_.ai_service->GetStats());
    root["last_result"] =
        AiResultToJson(dependencies_.ai_service->GetLastResult());
    return JsonResponse(200, root);
  }

  HttpResponse HandleSnapshot(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
      return StatusResponse(401, "Unauthorized");
    }
    if (dependencies_.snapshot_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    const std::string prefix = "/api/snapshot/";
    std::string name = request.path.substr(prefix.size());
    const size_t dot = name.find('.');
    if (dot != std::string::npos) {
      name = name.substr(0, dot);
    }
    StreamId stream_id = StreamId::kMain;
    if (!StreamIdFromJsonString(name, &stream_id)) {
      return StatusResponse(400, "Invalid stream");
    }
    CaptureRequest capture_request;
    capture_request.stream_id = stream_id;
    SnapshotFrame frame =
        dependencies_.snapshot_service->Capture(capture_request);
    if (!frame.buffer || frame.offset + frame.size > frame.buffer->Size()) {
      return StatusResponse(500, "Invalid snapshot frame");
    }
    HttpResponse response;
    response.status_code = 200;
    response.headers["Content-Type"] = "image/jpeg";
    const uint8_t *data = frame.buffer->Data() + frame.offset;
    response.body.assign(reinterpret_cast<const char *>(data), frame.size);
    return response;
  }

  HttpResponse HandleWebrtc(const HttpRequest &request) {
    AuthPrincipal principal = Authenticate(request);
    if (principal.user_name.empty()) {
      return StatusResponse(401, "Unauthorized");
    }
    if (dependencies_.webrtc_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    ConfigJson body = request.body.empty()
                          ? ConfigJson::object()
                          : ConfigJson::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
      return StatusResponse(400, "Invalid JSON");
    }

    if (request.path == "/api/webrtc/peers" &&
        request.method == HttpMethod::kPost) {
      WebrtcCreatePeerRequest create_request;
      std::string stream;
      StreamId stream_id = StreamId::kMain;
      if (!json_utils::Load(body, "stream", &stream) ||
          !StreamIdFromJsonString(stream, &stream_id) ||
          !json_utils::Load(body, "client_id", &create_request.client_id)) {
        return StatusResponse(400, "Invalid stream");
      }
      create_request.stream_id = stream_id;
      auto peer = dependencies_.webrtc_service->CreatePeer(create_request);
      if (peer.peer_id.empty()) {
        return StatusResponse(409, "Could not create peer");
      }
      ConfigJson root = ConfigJson::object();
      root["peer_id"] = peer.peer_id;
      root["stream"] = StreamIdToJsonString(peer.stream_id);
      return JsonResponse(200, root);
    }
    if (request.path == "/api/webrtc/offer" &&
        request.method == HttpMethod::kPost) {
      WebrtcOfferRequest offer;
      if (!json_utils::Load(body, "peer_id", &offer.peer_id) ||
          !json_utils::Load(body, "sdp", &offer.sdp)) {
        return StatusResponse(400, "Missing offer fields");
      }
      auto answer = dependencies_.webrtc_service->HandleOffer(offer);
      if (answer.sdp.empty()) {
        return StatusResponse(404, "Peer not found");
      }
      ConfigJson root = ConfigJson::object();
      root["peer_id"] = answer.peer_id;
      root["sdp"] = answer.sdp;
      return JsonResponse(200, root);
    }
    if (request.path == "/api/webrtc/candidate" &&
        request.method == HttpMethod::kPost) {
      WebrtcIceCandidate candidate;
      if (!json_utils::Load(body, "peer_id", &candidate.peer_id) ||
          !json_utils::Load(body, "candidate", &candidate.candidate) ||
          !json_utils::Load(body, "sdp_mid", &candidate.sdp_mid) ||
          !json_utils::Load(body, "sdp_mline_index", &candidate.sdp_mline_index,
                            0, std::numeric_limits<int32_t>::max())) {
        return StatusResponse(400, "Missing candidate fields");
      }
      return dependencies_.webrtc_service->AddIceCandidate(candidate)
                 ? OkResponse()
                 : StatusResponse(404, "Peer not found");
    }
    if (request.path == "/api/webrtc/close" &&
        request.method == HttpMethod::kPost) {
      std::string peer_id;
      if (!json_utils::Load(body, "peer_id", &peer_id)) {
        return StatusResponse(400, "Missing peer_id");
      }
      return dependencies_.webrtc_service->ClosePeer(peer_id)
                 ? OkResponse()
                 : StatusResponse(404, "Peer not found");
    }
    return StatusResponse(404, "Not Found");
  }

  bool IsWrappedConfigPayload(const std::string &name,
                              const ConfigJson &value) {
    return value.is_object() && value.size() == 1 && value.contains(name) &&
           value.at(name).is_object();
  }

  std::string FormatConfigError(const ConfigError &error) {
    if (error.reason.empty()) {
      return "set config failed";
    }
    if (error.field.empty()) {
      return error.reason;
    }
    return error.field + ": " + error.reason;
  }

  HttpResponse HandleConfig(const HttpRequest &request) {
    const std::string name =
        request.path.substr(std::string("/api/config/").size());
    if (name.empty()) {
      return StatusResponse(400, "Missing config name");
    }
    if (request.method == HttpMethod::kGet) {
      AuthPrincipal principal = Authenticate(request);
      if (principal.user_name.empty()) {
        return StatusResponse(401, "Unauthorized");
      }
      ConfigJson config = dependencies_.config_service->GetValue(name);
      if (config.is_null()) {
        return StatusResponse(404, "Not Found");
      }
      HttpResponse response;
      response.status_code = 200;
      response.headers["Content-Type"] = "application/json";
      response.body = config.dump();
      return response;
    }
    if (request.method == HttpMethod::kPut) {
      AuthPrincipal principal;
      if (!RequirePermission(request, AuthPermission::kModifyConfig, name,
                             &principal)) {
        return StatusResponse(403, "Forbidden");
      }
      ConfigJson config = ConfigJson::parse(request.body, nullptr, false);
      if (config.is_discarded()) {
        return StatusResponse(400, "Invalid JSON");
      }
      if (!config.is_object()) {
        return StatusResponse(400, "Config payload must be an object");
      }
      if (IsWrappedConfigPayload(name, config)) {
        return StatusResponse(400, "Config payload must be the top-level node");
      }
      bool ok = dependencies_.config_service->SetValue(name, config);
      std::string failure_reason;
      if (!ok) {
        failure_reason = FormatConfigError(
            dependencies_.config_service->GetLastConfigError(name));
      }
      RecordOperation(request, principal, OperationAction::kModifyConfig, name,
                      ok ? OperationResult::kSuccess : OperationResult::kFailed,
                      ok ? std::string() : failure_reason);
      if (!ok) {
        return StatusResponse(400, failure_reason);
      }
      return OkResponse();
    }
    return StatusResponse(404, "Not Found");
  }

  HttpResponse HandleOperations(const HttpRequest &request) {
    if (dependencies_.logger_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kManageUsers, "operations",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    OperationLogQuery query;
    query.limit = 100;
    std::vector<OperationRecord> records =
        dependencies_.logger_service->QueryOperations(query);
    ConfigJson root = ConfigJson::object();
    ConfigJson items = ConfigJson::array();
    for (const OperationRecord &record : records) {
      items.push_back(OperationRecordToJson(record));
    }
    root["items"] = items;
    return JsonResponse(200, root);
  }

  HttpResponse HandleOperationsExport(const HttpRequest &request) {
    if (dependencies_.logger_service == nullptr) {
      return StatusResponse(501, "Not Implemented");
    }
    AuthPrincipal principal;
    if (!RequirePermission(request, AuthPermission::kManageUsers, "operations",
                           &principal)) {
      return StatusResponse(403, "Forbidden");
    }
    OperationLogQuery query;
    query.limit = 1000;
    std::vector<OperationRecord> records =
        dependencies_.logger_service->QueryOperations(query);

    std::string body =
        "timestamp_ms,request_id,user_name,session_id,client_ip,module,"
        "action,target,result,reason\n";
    for (const OperationRecord &record : records) {
      body += std::to_string(record.timestamp_ms) + ",";
      body += record.request_id + ",";
      body += record.user_name + ",";
      body += record.session_id + ",";
      body += record.client_ip + ",";
      body += record.module + ",";
      body += OperationActionToString(record.action);
      body += ",";
      body += record.target + ",";
      body += OperationResultToString(record.result);
      body += ",";
      body += record.reason + "\n";
    }

    HttpResponse response;
    response.status_code = 200;
    response.headers["Content-Type"] = "text/csv";
    response.headers["Content-Disposition"] =
        "attachment; filename=\"operations.csv\"";
    response.body = std::move(body);
    return response;
  }

  HttpResponse HandleStaticFile(const HttpRequest &request) {
    if (options_.static_root.empty()) {
      IncrementNotFound();
      return StatusResponse(404, "Not Found");
    }
    if (IsUnsafeStaticPath(request.path)) {
      return StatusResponse(403, "Forbidden");
    }
    std::string relative =
        request.path == "/" ? "index.html" : request.path.substr(1);
    if (relative.empty()) {
      relative = "index.html";
    }
    const std::string path = infra::Path::Join(options_.static_root, relative);
    std::string content = infra::File::ReadAll(path);
    if (content.empty()) {
      IncrementNotFound();
      return StatusResponse(404, "Not Found");
    }
    HttpResponse response;
    response.status_code = 200;
    response.headers["Content-Type"] = ContentTypeForPath(path);
    response.body = content;
    return response;
  }

  void OnConnection(ConnectionId connection_id, NetAddress peer) {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      HttpSession session;
      session.client_ip = std::move(peer.ip);
      sessions_[connection_id] = session;
      ++stats_.active_connections;
    }
    ArmSessionTimer(connection_id, options_.request_timeout_ms);
  }

  void OnClose(ConnectionId connection_id) {
    (void)connection_id;
    std::lock_guard<std::mutex> guard(mutex_);
    sessions_.erase(connection_id);
    if (stats_.active_connections > 0) {
      --stats_.active_connections;
    }
  }

  void OnMessage(ConnectionId connection_id, const uint8_t *data,
                 uint32_t size) {
    if (data == nullptr) {
      return;
    }
    HttpResponse close_response;
    bool should_close = false;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      auto iter = sessions_.find(connection_id);
      if (iter == sessions_.end() || iter->second.closing) {
        return;
      }
      ++iter->second.timeout_generation;
      iter->second.recv_buffer.append(reinterpret_cast<const char *>(data),
                                      size);
      should_close = !ParsePendingRequestsLocked(iter, &close_response);
    }

    if (should_close) {
      IncrementParseFailures();
      SendResponse(connection_id, close_response, true);
      return;
    }
    ArmSessionTimer(connection_id, options_.request_timeout_ms);
    TryPostNextRequest(connection_id);
  }

  void TryPostNextRequest(ConnectionId connection_id) {
    PendingRequest pending;
    infra::Executor *task_executor = nullptr;
    bool has_request = false;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      auto iter = sessions_.find(connection_id);
      if (iter == sessions_.end() || iter->second.processing ||
          iter->second.pending_requests.empty()) {
        return;
      }
      pending = std::move(iter->second.pending_requests.front());
      iter->second.pending_requests.pop_front();
      iter->second.processing = true;
      has_request = true;
      task_executor = task_executor_.get();
    }
    if (!has_request) {
      return;
    }
    if (task_executor == nullptr ||
        task_executor->Post([this, connection_id,
                             pending = std::move(pending)]() mutable {
          HttpResponse handled = HandleRequest(pending.request);
          SendResponse(connection_id, handled, pending.close_after_response);
        }) == false) {
      SendResponse(connection_id, StatusResponse(503, "Service Unavailable"),
                   true);
    }
  }

  void SendResponseAndClose(ConnectionId connection_id,
                            const HttpResponse &response) {
    SendResponse(connection_id, response, true);
  }

  void SendResponse(ConnectionId connection_id, const HttpResponse &response,
                    bool close_after_response) {
    NetEngine *net_engine = nullptr;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      net_engine = dependencies_.net_engine;
    }
    if (net_engine == nullptr) {
      return;
    }
    HttpResponse response_copy = response;
    response_copy.headers["Connection"] =
        close_after_response ? "close" : "keep-alive";
    const std::string serialized = SerializeResponse(response_copy);
    if (!net_engine->Send(connection_id,
                          reinterpret_cast<const uint8_t *>(serialized.data()),
                          serialized.size())) {
      (void)net_engine->Close(connection_id);
      return;
    }
    if (close_after_response) {
      (void)net_engine->CloseAfterSend(connection_id);
      return;
    }
    CompleteKeepAliveRequest(connection_id);
  }

  void CompleteKeepAliveRequest(ConnectionId connection_id) {
    HttpResponse close_response;
    bool should_close = false;
    bool has_pending = false;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      auto iter = sessions_.find(connection_id);
      if (iter == sessions_.end()) {
        return;
      }
      iter->second.processing = false;
      should_close = !ParsePendingRequestsLocked(iter, &close_response);
      has_pending = !iter->second.pending_requests.empty();
    }
    if (should_close) {
      IncrementParseFailures();
      SendResponse(connection_id, close_response, true);
      return;
    }
    if (has_pending) {
      TryPostNextRequest(connection_id);
      return;
    }
    ArmSessionTimer(connection_id, options_.connection_idle_timeout_ms);
  }

  bool
  ParsePendingRequestsLocked(std::map<ConnectionId, HttpSession>::iterator iter,
                             HttpResponse *close_response) {
    HttpSession &session = iter->second;
    const size_t max_buffer_size =
        static_cast<size_t>(options_.max_request_header_bytes) + 4 +
        options_.max_request_body_bytes;
    size_t parsed_count = 0;
    while (!session.closing &&
           parsed_count <
               static_cast<size_t>(options_.max_pipelined_requests)) {
      if (session.recv_buffer.empty()) {
        return true;
      }
      if (session.recv_buffer.size() > max_buffer_size) {
        session.closing = true;
        if (close_response != nullptr) {
          *close_response = StatusResponse(413, "Payload Too Large");
        }
        return false;
      }
      RawParseResult parsed = ParseRawRequest(
          session.recv_buffer, options_.max_request_header_bytes,
          options_.max_request_body_bytes, session.client_ip);
      if (parsed.status == RawParseStatus::kIncomplete) {
        return true;
      }
      if (parsed.status != RawParseStatus::kComplete ||
          parsed.consumed_bytes == 0 ||
          parsed.consumed_bytes > session.recv_buffer.size()) {
        session.closing = true;
        if (close_response != nullptr) {
          *close_response = parsed.status == RawParseStatus::kPayloadTooLarge
                                ? StatusResponse(413, "Payload Too Large")
                                : StatusResponse(400, "Bad Request");
        }
        return false;
      }

      session.recv_buffer.erase(0, parsed.consumed_bytes);
      ++session.request_count;
      PendingRequest pending;
      pending.request = std::move(parsed.request);
      pending.close_after_response =
          !options_.enable_keep_alive || !parsed.keep_alive ||
          session.request_count >= options_.max_requests_per_connection;
      session.pending_requests.push_back(std::move(pending));
      ++parsed_count;
      if (session.pending_requests.back().close_after_response) {
        session.closing = true;
        session.recv_buffer.clear();
        return true;
      }
    }
    return true;
  }

  void ArmSessionTimer(ConnectionId connection_id, uint32_t delay_ms) {
    NetEngine *net_engine = nullptr;
    uint64_t generation = 0;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      auto iter = sessions_.find(connection_id);
      if (iter == sessions_.end()) {
        return;
      }
      generation = ++iter->second.timeout_generation;
      net_engine = dependencies_.net_engine;
    }
    if (net_engine == nullptr) {
      return;
    }
    (void)net_engine->RunOnIoAfter(
        delay_ms, [this, connection_id, generation]() {
          NetEngine *engine = nullptr;
          bool should_close = false;
          {
            std::lock_guard<std::mutex> guard(mutex_);
            auto iter = sessions_.find(connection_id);
            should_close = iter != sessions_.end() &&
                           iter->second.timeout_generation == generation;
            engine = dependencies_.net_engine;
          }
          if (should_close && engine != nullptr) {
            (void)engine->Close(connection_id);
          }
        });
  }

  HttpServiceOptions options_;
  HttpServiceDependencies dependencies_;
  mutable std::mutex mutex_;
  std::unique_ptr<infra::Executor> task_executor_;
  TcpServerId tcp_server_id_ = 0;
  std::map<ConnectionId, HttpSession> sessions_;
  HttpServiceStats stats_;
  uint64_t next_request_id_ = 0;
  bool initialized_ = false;
  bool started_ = false;
};

std::unique_ptr<IHttpService>
CreateHttpService(const HttpServiceOptions &options,
                  const HttpServiceDependencies &dependencies) {
  return std::unique_ptr<IHttpService>(
      new HttpServiceImpl(options, dependencies));
}

} // namespace live_stream
