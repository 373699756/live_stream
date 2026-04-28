export type StreamName = 'main' | 'sub';
export type Resolution = string;
export type VideoCodecName = 'h264' | 'h265' | 'mjpeg';

export interface VideoStreamConfig {
  enabled: boolean;
  name: StreamName;
  codec: VideoCodecName;
  profile: string;
  h265_profile: string;
  resolution: Resolution;
  fps: number;
  bitrate_kbps: number;
  rate_control: 'cbr' | 'vbr' | 'fixqp';
  gop: number;
  vbr_quality: number;
  smart_codec: boolean;
}

export interface VideoConfig {
  streams: {
    main: VideoStreamConfig;
    sub: VideoStreamConfig;
  };
  source: {
    sensor: string;
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
  codecs: CodecCapability[];
  resolutions: VideoResolutionCapability[];
  fps: NumberRange;
  bitrate_kbps: NumberRange;
  rate_control: VideoStreamConfig['rate_control'][];
  gop: NumberRange;
  smart_codec: boolean;
}

export interface MediaCapabilities {
  streams: {
    main: VideoStreamCapabilities;
    sub: VideoStreamCapabilities;
  };
}

export interface ImageConfig {
  basic: Record<string, number>;
  exposure: Record<string, unknown>;
  white_balance: Record<string, unknown>;
  backlight: Record<string, unknown>;
  enhancement: Record<string, unknown>;
  orientation: { mirror: boolean; flip: boolean };
  color_mode: { mode: string };
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

export interface StreamStatus {
  stream: StreamName;
  codec: string;
  resolution: string;
  fps: number;
  bitrateKbps: number;
  state: 'running' | 'stopped' | 'error';
}
