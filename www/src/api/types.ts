export type StreamName = 'main' | 'sub';
export type Resolution = string;
export type VideoCodecName = 'h264' | 'h265' | 'jpeg' | 'mjpeg';
export type GopMode = 'normal_p' | 'dual_p' | 'smart_p';
export type ImageStrategyMode = 'balanced' | 'low_noise' | 'detail';

export interface AuthPrincipal {
  user_name: string;
  session_id: string;
  role: string;
  must_change_password?: boolean;
}

export interface AuthState {
  authenticated: boolean;
  mustChangePassword: boolean;
  principal?: AuthPrincipal;
}

export interface VideoStreamConfig {
  enabled: boolean;
  codec: VideoCodecName;
  resolution: Resolution;
  fps: number;
  bitrate_kbps: number;
  rate_control: 'cbr' | 'vbr' | 'fixqp';
  gop: number;
  gop_mode: GopMode;
  smart_codec: boolean;
  roi: VideoRoiConfig;
}

export interface VideoRoiRegion {
  enabled: boolean;
  x: number;
  y: number;
  width: number;
  height: number;
  qp: number;
  absolute_qp: boolean;
}

export interface VideoRoiConfig {
  enabled: boolean;
  regions: VideoRoiRegion[];
}

export interface VideoConfig {
  streams: {
    main: VideoStreamConfig;
    sub: VideoStreamConfig;
  };
}

export interface CodecCapability {
  codec: VideoCodecName;
  profiles: string[];
}

export interface VideoResolutionCapability {
  width: number;
  height: number;
}

export interface NumberRange {
  min: number;
  max: number;
}

export interface VideoStreamCapabilities {
  stream: StreamName;
  available: boolean;
  codecs: CodecCapability[];
  resolutions: VideoResolutionCapability[];
  fps: NumberRange;
  bitrate_kbps: NumberRange;
  rate_control: VideoStreamConfig['rate_control'][];
  gop: NumberRange;
  smart_codec: boolean;
  roi_supported: boolean;
  max_roi_regions: number;
}

export interface NumericControlCapability {
  min: number;
  max: number;
  default: number;
  runtime_supported?: boolean;
}

export interface OptionControlCapability {
  values: string[];
  default: string;
  runtime_supported?: boolean;
}

export interface ImageCapabilities {
  basic: Record<string, NumericControlCapability>;
  exposure: {
    options: Record<string, OptionControlCapability>;
    ranges: Record<string, NumericControlCapability>;
  };
  white_balance: {
    options: Record<string, OptionControlCapability>;
    ranges: Record<string, NumericControlCapability>;
  };
  enhancement: {
    options: Record<string, OptionControlCapability>;
    ranges: Record<string, NumericControlCapability>;
  };
  backlight: {
    options: Record<string, OptionControlCapability>;
    ranges: Record<string, NumericControlCapability>;
  };
  color_mode: Record<string, OptionControlCapability>;
  lens_correction?: {
    supported: boolean;
    min_width: number;
    min_height: number;
    options: Record<string, OptionControlCapability>;
    ranges: Record<string, NumericControlCapability>;
  };
  stabilization?: {
    supported: boolean;
    min_width: number;
    min_height: number;
    options: Record<string, OptionControlCapability>;
    ranges: Record<string, NumericControlCapability>;
  };
  orientation: { mirror: boolean; flip: boolean };
}

export interface MediaCapabilities {
  streams: {
    main: VideoStreamCapabilities;
    sub: VideoStreamCapabilities;
  };
  image: ImageCapabilities;
}

export interface ImageConfig {
  basic: Record<string, number>;
  exposure: Record<string, unknown>;
  white_balance: Record<string, unknown>;
  backlight: Record<string, unknown>;
  enhancement: Record<string, unknown>;
  orientation: { mirror: boolean; flip: boolean };
  color_mode: Record<string, string>;
  lens_correction?: {
    enabled: boolean;
    aspect: boolean;
    x_ratio: number;
    y_ratio: number;
    xy_ratio: number;
    center_x_offset: number;
    center_y_offset: number;
    distortion_ratio: number;
  };
  stabilization?: {
    enabled: boolean;
    motion_level: string;
    crop_ratio: number;
    buffer_count: number;
    frame_rate: number;
    moving_subject_level: number;
    rolling_shutter_coef: number;
    horizontal_limit: number;
    vertical_limit: number;
  };
  strategy?: {
    enabled: boolean;
    mode: ImageStrategyMode;
  };
}

export interface ImageStrategyStatus {
  enabled: boolean;
  active: boolean;
  exposure_valid: boolean;
  iso: number;
  exposure_time_us: number;
  analog_gain: number;
  digital_gain: number;
  isp_digital_gain: number;
  mode: string;
  tier: string;
  saturation: number;
  sharpness: number;
  denoise_2d: number;
  denoise_3d: number;
  gamma: number;
}

export interface PrivacyMaskConfig {
  enabled: boolean;
  x: number;
  y: number;
  width: number;
  height: number;
  color: string;
}

export interface OverlayConfig {
  enabled: boolean;
  items: {
    timestamp: { enabled: boolean; format: string; x: number; y: number };
    device_name: { enabled: boolean; text: string; x: number; y: number };
  };
  font_size: number;
  font_color: string;
  background: boolean;
  privacy_masks: {
    main: PrivacyMaskConfig[];
    sub: PrivacyMaskConfig[];
  };
}

export interface NetworkConfig {
  hostname: string;
  interfaces: Record<string, unknown>;
  ports: Record<string, number>;
}

export interface SnapshotConfig {
  enabled: boolean;
  jpeg_quality: number;
  timeout_ms: number;
}

export interface RtspConfig {
  enabled: boolean;
  port: number;
  auth_required: boolean;
  max_sessions: number;
  session_timeout_sec: number;
}

export interface WebrtcConfig {
  enabled: boolean;
  local_port_base: number;
  public_ip: string;
  ice_servers: Array<{
    url: string;
    username?: string;
    credential?: string;
  }>;
  max_peers: number;
  prefer_tcp: boolean;
}

export type WebrtcPeerState =
  | 'created'
  | 'offer_received'
  | 'connecting'
  | 'connected'
  | 'closing'
  | 'closed'
  | 'failed'
  | 'unknown';

export interface MediaStreamRuntime {
  stream: StreamName;
  available: boolean;
  running: boolean;
  codec: string;
  codec_generation: number;
  track_ready: boolean;
  hls_supported: boolean;
  hls_ready: boolean;
  http_flv_supported: boolean;
  http_flv_ready: boolean;
  mjpeg_supported: boolean;
  mjpeg_ready: boolean;
  webrtc_supported: boolean;
  webrtc_ready: boolean;
  reader_count: number;
  client_count: number;
  cached_frames: number;
  cached_bytes: number;
  hls_bytes: number;
  last_dts: number;
  last_keyframe_request_ms: number;
  last_keyframe_seen_ms: number;
  last_first_frame_ms: number;
  last_protocol_ready_ms: number;
  last_reset_reason: string;
  resolution?: string;
  fps?: number;
  bitrate_kbps?: number;
}

export interface MediaStreamsResponse {
  items: MediaStreamRuntime[];
}

export interface MediaPlaybackUrls {
  stream: StreamName;
  rtsp: string;
  hls: string;
  http_flv: string;
  mjpeg: string;
  snapshot: string;
  webrtc_whep?: string;
}

export interface MediaSessionInfo {
  session_id: string;
  protocol: string;
  stream: StreamName;
  state: string;
  stream_state?: 'opening' | 'attached' | 'closing' | 'none' | string;
  connection_id?: number;
  client_id?: string;
  client_ip?: string;
  user_name?: string;
  peer_id?: string;
  transport?: string;
  remote_address?: string;
  local_address?: string;
  reader_id?: number;
  ice_selected?: boolean;
  dtls_state?: string;
  srtp_ready?: boolean;
  rtp_packets?: number;
  rtp_bytes?: number;
  rtcp_packets?: number;
  rtcp_bytes?: number;
  rtcp_pli_count?: number;
  rtcp_fir_count?: number;
  rtcp_nack_count?: number;
  rtcp_transport_cc_count?: number;
  rtcp_keyframe_requests?: number;
  last_rtcp_ms?: number;
  last_error?: string;
  pending_bytes?: number;
  send_queue_length?: number;
  last_write_at_ms?: number;
  close_reason?: string;
  created_at_ms?: number;
  updated_at_ms?: number;
}

export interface MediaSessionsResponse {
  items: MediaSessionInfo[];
  http_flv_active_clients?: number;
  mjpeg_active_clients?: number;
  rtsp_active_sessions?: number;
  webrtc_active_peers?: number;
  webrtc_dtls_ready?: boolean;
  webrtc_enabled?: boolean;
  webrtc_ice_ready?: boolean;
  webrtc_ice_server_count?: number;
  webrtc_local_port_base?: number;
  webrtc_max_peers?: number;
  webrtc_public_ip?: string;
  webrtc_selected_ice_pairs?: number;
  webrtc_signaling_ready?: boolean;
  webrtc_srtp_ready?: boolean;
}

export interface WebrtcPeerInfo {
  peer_id: string;
  stream: StreamName;
  codec: string;
  state: WebrtcPeerState;
  client_id: string;
  session_id: string;
  user_name: string;
  client_ip: string;
  ice_selected: boolean;
  dtls_state: string;
  srtp_ready: boolean;
  rtp_packets: number;
  rtp_bytes: number;
  rtcp_packets: number;
  rtcp_bytes: number;
  rtcp_pli_count: number;
  rtcp_fir_count: number;
  rtcp_nack_count: number;
  rtcp_transport_cc_count: number;
  rtcp_keyframe_requests: number;
  last_error: string;
  created_at_ms: number;
  updated_at_ms: number;
}

export interface WebrtcOfferAnswer {
  peer_id: string;
  sdp: string;
  state: WebrtcPeerState;
}

export type AiBackendName = 'hisi3516dv300_nnie' | 'host_stub';
export type AiTaskName =
  | 'object_detection'
  | 'perimeter_detection'
  | 'motion_classification'
  | 'occlusion_detection';

export interface AiPerimeterRegion {
  name: string;
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface AiModelConfig {
  enabled: boolean;
  backend: AiBackendName;
  task: AiTaskName;
  stream: StreamName;
  model_path: string;
  input_width: number;
  input_height: number;
  inference_interval_ms: number;
  confidence_threshold: number;
  max_results: number;
  perimeter_regions: AiPerimeterRegion[];
}

export interface AiConfig {
  enabled: boolean;
  tasks: AiModelConfig[];
}

export interface AiDetection {
  label: string;
  confidence: number;
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface AiStats {
  enabled: boolean;
  backend_available: boolean;
  alarm_linked: boolean;
  last_success_time_ms: number;
  last_failure_time_ms: number;
  received_frames: number;
  skipped_frames: number;
  inference_count: number;
  inference_failed_count: number;
  dropped_tasks: number;
  last_inference_time_ms: number;
  max_inference_time_ms: number;
  average_inference_time_ms: number;
  active_results: number;
}

export interface AiInferenceResult {
  success: boolean;
  stream: StreamName;
  sequence: number;
  pts_us: number;
  detections: AiDetection[];
}

export interface AiTaskStatus {
  config: AiModelConfig;
  stats: AiStats;
  last_result: AiInferenceResult;
}

export interface AiStatus {
  enabled: boolean;
  config: AiConfig;
  summary: AiStats;
  tasks: AiTaskStatus[];
  last_result: AiInferenceResult;
}

export interface AiAlertRecord {
  id: string;
  timestamp_ms: number;
  stream: StreamName;
  task: AiTaskName;
  image_url: string;
  detection_count: number;
  confidence_max: number;
  detections: AiDetection[];
}

export interface AiAlertList {
  items: AiAlertRecord[];
}

export interface AlarmRuleConfig {
  enabled: boolean;
  sensitivity: number;
  min_duration_ms: number;
  regions: unknown[];
  [key: string]: unknown;
}

export interface AlarmActionsConfig {
  snapshot: boolean;
  notify: boolean;
  [key: string]: unknown;
}

export interface AlarmScheduleConfig {
  mode: string;
  weekly: unknown[];
  [key: string]: unknown;
}

export interface AlarmConfig {
  motion_detection: AlarmRuleConfig;
  ai_detection: AlarmRuleConfig;
  actions: AlarmActionsConfig;
  schedule: AlarmScheduleConfig;
  [key: string]: unknown;
}

export type AlarmSourceName =
  | 'motion'
  | 'ai_detection'
  | 'io_input'
  | 'tamper'
  | 'network'
  | 'unknown';

export interface AlarmRuntimeStatus {
  active: boolean;
  source: AlarmSourceName;
  active_since_ms: number;
  last_trigger_time_ms: number;
  message: string;
}

export interface AlarmStatusResponse {
  available: boolean;
  status: AlarmRuntimeStatus;
}

export type UpgradeState =
  | 'idle'
  | 'validating'
  | 'preparing'
  | 'writing'
  | 'committing'
  | 'waiting_reboot'
  | 'completed'
  | 'failed'
  | 'canceled';

export interface UpgradePackageInfo {
  package_path: string;
  version: string;
  size_bytes: number;
  digest: string;
  build_time_ms: number;
  target_model: string;
  requires_reboot: boolean;
}

export interface UpgradeStatus {
  state: UpgradeState;
  progress_percent: number;
  current_stage: string;
  target_version: string;
  ok: boolean;
  error_message: string;
  started_at_ms: number;
  finished_at_ms: number;
}

export interface UpgradeRequest {
  package_path: string;
  expected_version: string;
  allow_same_version: boolean;
  allow_downgrade: boolean;
  auto_reboot: boolean;
}

export interface SystemStatus {
  deviceName: string;
  model: string;
  firmware: string;
  uptime: string;
  cpu: number;
  memory: number;
  temperature: number;
  modules: Array<{ name: string; state: 'running' | 'pending' | 'error' }>;
}

export type TimeSyncSource = 'manual' | 'onvif' | 'ntp' | 'browser' | 'unknown';

export interface NtpConfig {
  enabled: boolean;
  servers: string[];
  sync_interval_sec: number;
}

export interface TimeStatus {
  system_time_ms: number;
  timezone: string;
  ntp: NtpConfig;
  manual_sync_allowed: boolean;
  browser_sync_on_login: boolean;
  last_sync_source: TimeSyncSource;
  last_sync_time_ms: number;
  last_sync_ok: boolean;
}

export interface TimeConfig {
  timezone: string;
  ntp: NtpConfig;
  manual_sync_allowed: boolean;
  browser_sync_on_login: boolean;
}

export interface BrowserSyncConfig {
  manual_sync_allowed: boolean;
  browser_sync_on_login: boolean;
}

export type OperationAction =
  | 'Login'
  | 'Logout'
  | 'AuthFailed'
  | 'TokenExpired'
  | 'ModifyConfig'
  | 'Reboot'
  | 'FactoryReset'
  | 'Upgrade'
  | 'TimeSync'
  | 'NetworkChange'
  | 'UserManage'
  | 'PermissionDenied';

export type OperationResult = 'Success' | 'Failed' | 'Rejected';

export interface OperationRecord {
  timestamp_ms: number;
  request_id: string;
  user_name: string;
  session_id: string;
  client_ip: string;
  module: string;
  action: OperationAction;
  target: string;
  result: OperationResult;
  reason: string;
}
