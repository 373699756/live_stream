export type StreamName = 'main' | 'sub';
export type Resolution = string;
export type VideoCodecName = 'h264' | 'h265' | 'jpeg' | 'mjpeg';

export interface VideoStreamConfig {
  enabled: boolean;
  codec: VideoCodecName;
  resolution: Resolution;
  fps: number;
  bitrate_kbps: number;
  rate_control: 'cbr' | 'vbr' | 'fixqp';
  gop: number;
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
  strategy?: {
    enabled: boolean;
    mode: 'balanced';
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

export interface OsdConfig {
  enabled: boolean;
  items: {
    timestamp: { enabled: boolean; format: string; x: number; y: number };
    device_name: { enabled: boolean; text: string; x: number; y: number };
  };
  font_size: number;
  font_color: string;
  background: boolean;
}

export interface NetworkConfig {
  hostname: string;
  interfaces: Record<string, unknown>;
  ports: Record<string, number>;
}

export interface SnapshotConfig {
  enabled: boolean;
  main_path: string;
  sub_path: string;
  jpeg_quality: number;
  timeout_ms: number;
}

export interface RtspConfig {
  enabled: boolean;
  port: number;
  auth_required: boolean;
  paths: {
    main: string;
    sub: string;
  };
  max_sessions: number;
  session_timeout_sec: number;
}

export interface WebrtcConfig {
  enabled: boolean;
  signaling_path: string;
  ice_servers: Array<{
    url: string;
    username?: string;
    credential?: string;
  }>;
  max_peers: number;
  prefer_tcp: boolean;
}

export interface StreamStatus {
  stream: StreamName;
  codec: string;
  resolution: string;
  fps: number;
  bitrateKbps: number;
  state: 'running' | 'stopped' | 'pending';
  browserCodec?: boolean;
  hlsReady?: boolean;
  flvReady?: boolean;
  webrtcReady?: boolean;
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
  services: Array<{ name: string; state: 'running' | 'pending' | 'error' }>;
}

export interface OperationRecord {
  timestamp_ms: number;
  request_id: string;
  user_name: string;
  session_id: string;
  client_ip: string;
  module: string;
  action: string;
  target: string;
  result: 'success' | 'failed' | 'rejected' | string;
  reason: string;
}
