#include "protocol_subsystem.h"

#include <string>

#include "core_services.h"
#include "device_subsystem.h"
#include "infra/log.h"
#include "media_subsystem.h"

namespace live_stream {
namespace {

class StaticOnvifUriProvider : public IOnvifUriProvider {
public:
  StaticOnvifUriProvider(const AppRuntimeConfig &config,
                         MediaService *media_service)
      : config_(config), media_service_(media_service) {}

  std::string GetStreamUri(StreamId stream_id) override {
    if (media_service_ != nullptr &&
        !media_service_->IsStreamStarted(stream_id)) {
      return std::string();
    }
    return std::string("rtsp://") + config_.advertise_host + ":" +
           std::to_string(config_.rtsp_port) + StreamPath(stream_id);
  }

  std::string GetSnapshotUri(StreamId stream_id) override {
    return std::string("http://") + config_.advertise_host + ":" +
           std::to_string(config_.http_port) + SnapshotPath(stream_id);
  }

private:
  const char *StreamPath(StreamId stream_id) const {
    return stream_id == StreamId::kSub ? "/live/sub" : "/live/main";
  }

  const std::string &SnapshotPath(StreamId stream_id) const {
    return stream_id == StreamId::kSub ? config_.snapshot_sub_path
                                       : config_.snapshot_main_path;
  }

  AppRuntimeConfig config_;
  MediaService *media_service_ = nullptr;
};

} // namespace

ProtocolSubsystem &ProtocolSubsystem::Get() {
  static ProtocolSubsystem subsystem;
  return subsystem;
}

bool ProtocolSubsystem::Start(const AppRuntimeConfig &runtime_config) {
  if (started_) {
    return true;
  }

  CoreServices &core = CoreServices::Get();
  const DeviceRefs device = DeviceSubsystem::Get().refs();
  const MediaRefs media = MediaSubsystem::Get().refs();

  net_engine_ = CreateNetEngine(NetEngineOptions{});
  if (!net_engine_ || !net_engine_->Start()) {
    INFRA_LOG_ERROR("app", "Start net engine failed");
    Stop();
    return false;
  }

  RtspServiceOptions rtsp_options;
  rtsp_options.listen_ip = runtime_config.listen_ip;
  rtsp_options.listen_port = runtime_config.rtsp_port;
  rtsp_options.max_sessions = runtime_config.rtsp_max_sessions;
  rtsp_options.enable_auth = runtime_config.rtsp_auth_required;
  rtsp_options.main_stream_codec = runtime_config.rtsp_main_codec;
  rtsp_options.sub_stream_codec = runtime_config.rtsp_sub_codec;
  RtspServiceDependencies rtsp_dependencies;
  rtsp_dependencies.net_engine = net_engine_.get();
  rtsp_dependencies.auth_service = core.auth();
  rtsp_dependencies.event_service = core.event();
  rtsp_dependencies.media_service = media.media;
  rtsp_ = CreateRtspService(rtsp_options, rtsp_dependencies);
  if (!rtsp_ || !rtsp_->Start()) {
    INFRA_LOG_ERROR("app", "Start rtsp service failed: listen=%s:%u",
                    runtime_config.listen_ip.c_str(),
                    static_cast<unsigned>(runtime_config.rtsp_port));
    Stop();
    return false;
  }

  WebrtcServiceOptions webrtc_options;
  webrtc_options.enabled = runtime_config.webrtc_enabled;
  webrtc_options.local_port_base = runtime_config.webrtc_local_port_base;
  webrtc_options.max_peers = runtime_config.webrtc_max_peers;
  webrtc_options.prefer_tcp = runtime_config.webrtc_prefer_tcp;
  webrtc_options.public_ip = runtime_config.advertise_host;
  webrtc_options.ice_servers = runtime_config.webrtc_ice_servers;
  WebrtcServiceDependencies webrtc_dependencies;
  webrtc_dependencies.net_engine = net_engine_.get();
  webrtc_dependencies.media_service = media.media;
  webrtc_dependencies.use_fake_engine = false;
  webrtc_ = CreateWebrtcService(webrtc_options, webrtc_dependencies);
  if (!webrtc_ || !webrtc_->Start()) {
    INFRA_LOG_ERROR(
        "app", "Start webrtc service failed: enabled=%d base=%u",
        runtime_config.webrtc_enabled ? 1 : 0,
        static_cast<unsigned>(runtime_config.webrtc_local_port_base));
    Stop();
    return false;
  }

  FrameServiceOptions frame_options;
  FrameServiceDependencies frame_dependencies;
  frame_dependencies.media_service = media.media;
  frame_ = CreateFrameService(frame_options, frame_dependencies);
  if (!frame_ || !frame_->Start()) {
    INFRA_LOG_ERROR("app", "Start frame service failed");
    Stop();
    return false;
  }

  onvif_uri_provider_.reset(
      new StaticOnvifUriProvider(runtime_config, media.media));
  OnvifServiceOptions onvif_options;
  onvif_options.listen_ip = runtime_config.listen_ip;
  onvif_options.advertise_ip = runtime_config.advertise_host;
  onvif_options.device_service_port = runtime_config.onvif_device_port;
  onvif_options.discovery_port = runtime_config.onvif_discovery_port;
  onvif_options.discovery_enabled = runtime_config.onvif_discovery_enabled;
  onvif_options.enable_auth = runtime_config.onvif_auth_required;
  onvif_options.manufacturer = runtime_config.onvif_manufacturer;
  onvif_options.model = runtime_config.onvif_model;
  onvif_options.firmware_version = runtime_config.onvif_firmware_version;
  OnvifServiceDependencies onvif_dependencies;
  onvif_dependencies.net_engine = net_engine_.get();
  onvif_dependencies.auth_service = core.auth();
  onvif_dependencies.config_service = core.config();
  onvif_dependencies.event_service = core.event();
  onvif_dependencies.system_service = device.system;
  onvif_dependencies.time_service = device.time;
  onvif_dependencies.uri_provider = onvif_uri_provider_.get();
  onvif_ = CreateOnvifService(onvif_options, onvif_dependencies);
  if (!onvif_ || !onvif_->Start()) {
    INFRA_LOG_ERROR("app", "Start onvif service failed: device_port=%u",
                    static_cast<unsigned>(runtime_config.onvif_device_port));
    Stop();
    return false;
  }

  HttpServiceOptions http_options;
  http_options.listen_ip = runtime_config.listen_ip;
  http_options.listen_port = runtime_config.http_port;
  http_options.static_root = runtime_config.static_root;
  http_options.enable_static_files = true;
  http_options.enable_keep_alive = true;
  http_options.max_requests_per_connection = 32;
  http_options.max_request_body_bytes = 128U * 1024U * 1024U;
  http_options.request_timeout_ms = 60000;
  http_options.connection_idle_timeout_ms = 60000;

  HttpServiceDependencies http_dependencies;
  http_dependencies.net_engine = net_engine_.get();
  http_dependencies.auth_service = core.auth();
  http_dependencies.config_service = core.config();
  http_dependencies.logger_service = core.logger();
  http_dependencies.network_service = device.network;
  http_dependencies.time_service = device.time;
  http_dependencies.alarm_service = device.alarm;
  http_dependencies.upgrade_service = device.upgrade;
  http_dependencies.rtsp_service = rtsp_.get();
  http_dependencies.onvif_service = onvif_.get();
  http_dependencies.system_service = device.system;
  http_dependencies.ai_service = media.ai;
  http_dependencies.media_service = media.media;
  http_dependencies.snapshot_service = media.snapshot;
  http_dependencies.webrtc_service = webrtc_.get();
  http_dependencies.frame_service = frame_.get();
  http_ = CreateHttpService(http_options, http_dependencies);
  if (!http_ || !http_->Start()) {
    INFRA_LOG_ERROR("app", "Start http service failed: listen=%s:%u root=%s",
                    runtime_config.listen_ip.c_str(),
                    static_cast<unsigned>(runtime_config.http_port),
                    runtime_config.static_root.c_str());
    Stop();
    return false;
  }

  started_ = true;
  return true;
}

void ProtocolSubsystem::Stop() {
  if (http_) {
    http_->Stop();
    http_.reset();
  }
  if (onvif_) {
    onvif_->Stop();
    onvif_.reset();
  }
  onvif_uri_provider_.reset();
  if (frame_) {
    frame_->Stop();
    frame_.reset();
  }
  if (webrtc_) {
    webrtc_->Stop();
    webrtc_.reset();
  }
  if (rtsp_) {
    rtsp_->Stop();
    rtsp_.reset();
  }
  if (net_engine_) {
    net_engine_->Stop();
    net_engine_.reset();
  }
  started_ = false;
}

ProtocolRefs ProtocolSubsystem::refs() const {
  ProtocolRefs refs;
  refs.rtsp = rtsp_.get();
  refs.webrtc = webrtc_.get();
  refs.onvif = onvif_.get();
  refs.http = http_.get();
  return refs;
}

} // namespace live_stream
