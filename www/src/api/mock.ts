import type {
  ImageConfig,
  ImageStrategyStatus,
  NetworkConfig,
  OsdConfig,
  SnapshotConfig,
  StreamStatus,
  SystemStatus,
  MediaCapabilities,
  UpgradePackageInfo,
  UpgradeStatus,
  VideoConfig,
  RtspConfig,
  WebrtcConfig,
} from './types';

// ---------------------------------------------------------------------------
// Domain: video  (api/video.ts)
// ---------------------------------------------------------------------------

export const mockVideoConfig: VideoConfig = {
  streams: {
    main: {
      enabled: true,
      codec: 'h264',
      resolution: '1920x1080',
      fps: 25,
      bitrate_kbps: 12288,
      rate_control: 'cbr',
      gop: 50,
    },
    sub: {
      enabled: true,
      codec: 'h264',
      resolution: '640x360',
      fps: 15,
      bitrate_kbps: 768,
      rate_control: 'cbr',
      gop: 30,
    },
  },
};

// ---------------------------------------------------------------------------
// Domain: video — media capabilities  (api/video.ts)
// ---------------------------------------------------------------------------

export const mockMediaCapabilities: MediaCapabilities = {
  streams: {
    main: {
      stream: 'main',
      available: true,
      codecs: [
        { codec: 'h264', profiles: ['baseline', 'main', 'high'] },
        { codec: 'h265', profiles: ['main'] },
      ],
      resolutions: [
        { width: 1920, height: 1080 },
        { width: 1280, height: 720 },
        { width: 704, height: 576 },
        { width: 640, height: 360 },
        { width: 352, height: 288 },
      ],
      fps: { min: 1, max: 30 },
      bitrate_kbps: { min: 512, max: 12288 },
      rate_control: ['cbr', 'vbr', 'fixqp'],
      gop: { min: 1, max: 120 },
      smart_codec: false,
    },
    sub: {
      stream: 'sub',
      available: true,
      codecs: [
        { codec: 'h264', profiles: ['baseline', 'main', 'high'] },
        { codec: 'h265', profiles: ['main'] },
      ],
      resolutions: [
        { width: 1280, height: 720 },
        { width: 704, height: 576 },
        { width: 640, height: 360 },
        { width: 352, height: 288 },
      ],
      fps: { min: 1, max: 30 },
      bitrate_kbps: { min: 64, max: 2048 },
      rate_control: ['cbr', 'vbr', 'fixqp'],
      gop: { min: 1, max: 120 },
      smart_codec: false,
    },
  },
  image: {
    basic: {
      saturation: { min: 0, max: 100, default: 50 },
      sharpness: { min: 0, max: 100, default: 55 },
    },
    exposure: {
      options: {
        mode: { values: ['auto', 'manual'], default: 'auto' },
        anti_flicker: { values: ['50hz', '60hz', 'off'], default: '50hz' },
        exposure_time: { values: ['auto', '1/25', '1/50', '1/100', '1/250'], default: 'auto' },
        gain: { values: ['auto', 'low', 'medium', 'high'], default: 'auto' },
        slow_shutter: { values: ['false', 'true'], default: 'false' },
      },
      ranges: { compensation: { min: 0, max: 100, default: 50 } },
    },
    white_balance: {
      options: {
        mode: { values: ['auto', 'manual'], default: 'auto' },
      },
      ranges: {
        red_gain: { min: 0, max: 100, default: 50 },
        blue_gain: { min: 0, max: 100, default: 50 },
      },
    },
    enhancement: {
      options: { defog: { values: ['false', 'true'], default: 'false' } },
      ranges: {
        denoise_2d: { min: 0, max: 100, default: 55 },
        denoise_3d: { min: 0, max: 100, default: 45 },
        gamma: { min: 0, max: 100, default: 50 },
      },
    },
    backlight: {
      options: {
        mode: { values: ['off', 'wdr'], default: 'off' },
      },
      ranges: { level: { min: 0, max: 100, default: 50 } },
    },
    color_mode: {},
    orientation: { mirror: true, flip: true },
  },
};

// ---------------------------------------------------------------------------
// Domain: image  (api/image.ts)
// ---------------------------------------------------------------------------

export const mockImageConfig: ImageConfig = {
  basic: { brightness: 50, contrast: 50, saturation: 50, sharpness: 55, hue: 50 },
  exposure: {
    mode: 'auto',
    anti_flicker: '50hz',
    exposure_time: 'auto',
    gain: 'auto',
    compensation: 50,
    slow_shutter: false,
    max_exposure_time: '1/25',
  },
  white_balance: { mode: 'auto', red_gain: 50, blue_gain: 45 },
  enhancement: { denoise_2d: 55, denoise_3d: 45, defog: false, gamma: 50 },
  backlight: { mode: 'off', level: 50 },
  orientation: { mirror: false, flip: false },
  color_mode: { mode: 'color' },
  strategy: { enabled: true, mode: 'balanced' },
};

export const mockImageStrategyStatus: ImageStrategyStatus = {
  enabled: true,
  active: true,
  exposure_valid: true,
  iso: 200,
  exposure_time_us: 10000,
  analog_gain: 1024,
  digital_gain: 1024,
  isp_digital_gain: 1024,
  mode: 'balanced',
  tier: 'day',
  saturation: 62,
  sharpness: 65,
  denoise_2d: 45,
  denoise_3d: 37,
  gamma: 50,
};

// ---------------------------------------------------------------------------
// Domain: system — OSD  (api/system.ts)
// ---------------------------------------------------------------------------

export const mockOsdConfig: OsdConfig = {
  enabled: true,
  items: {
    timestamp: { enabled: true, format: '%Y-%m-%d %H:%M:%S', x: 16, y: 16 },
    device_name: { enabled: true, text: 'IPC Camera', x: 16, y: 48 },
  },
  font_size: 24,
  font_color: '#FFFFFF',
  background: true,
};

// ---------------------------------------------------------------------------
// Domain: network  (api/network.ts)
// ---------------------------------------------------------------------------

export const mockNetworkConfig: NetworkConfig = {
  hostname: 'live-stream-ipc',
  interfaces: {
    eth0: {
      enabled: true,
      dhcp: true,
      static_ipv4: {
        address: '192.168.1.100',
        netmask: '255.255.255.0',
        gateway: '192.168.1.1',
      },
    },
  },
  ports: { http: 80, https: 443, rtsp: 554, onvif: 8000 },
};

// ---------------------------------------------------------------------------
// Domain: system — snapshot config  (api/system.ts)
// ---------------------------------------------------------------------------

export const mockSnapshotConfig: SnapshotConfig = {
  enabled: true,
  main_path: '/api/snapshot/main.jpg',
  sub_path: '/api/snapshot/sub.jpg',
  jpeg_quality: 85,
  timeout_ms: 2000,
};

// ---------------------------------------------------------------------------
// Domain: stream  (api/stream.ts)
// ---------------------------------------------------------------------------

export const mockRtspConfig: RtspConfig = {
  enabled: true,
  port: 554,
  auth_required: true,
  paths: {
    main: '/live/main',
    sub: '/live/sub',
  },
  max_sessions: 8,
  session_timeout_sec: 60,
};

export const mockWebrtcConfig: WebrtcConfig = {
  enabled: true,
  signaling_path: '/api/webrtc',
  ice_servers: [],
  max_peers: 4,
  prefer_tcp: false,
};

// ---------------------------------------------------------------------------
// Domain: system — upgrade  (api/system.ts)
// ---------------------------------------------------------------------------

export const mockUpgradePackageInfo: UpgradePackageInfo = {
  package_path: '/tmp/live_stream/upgrade/uploads/mock-firmware.bin',
  version: '1.0.1',
  size_bytes: 8 * 1024 * 1024,
  digest: 'mock-digest',
  build_time_ms: Date.now(),
  target_model: 'live_stream_ipc',
  requires_reboot: true,
};

export const mockUpgradeStatus: UpgradeStatus = {
  state: 'idle',
  progress_percent: 0,
  current_stage: 'idle',
  target_version: '',
  ok: true,
  error_message: '',
  started_at_ms: 0,
  finished_at_ms: 0,
};

// ---------------------------------------------------------------------------
// Domain: system — status & stream status  (api/system.ts / api/video.ts)
// ---------------------------------------------------------------------------

export const mockSystemStatus: SystemStatus = {
  deviceName: 'IPC Camera',
  model: 'live_stream_ipc',
  firmware: '0.1.0',
  uptime: '3d 06:18:42',
  cpu: 34,
  memory: 51,
  temperature: 48,
  services: [
    { name: 'config_service', state: 'running' },
    { name: 'auth_service', state: 'running' },
    { name: 'media_service', state: 'pending' },
    { name: 'http_service', state: 'running' },
    { name: 'webrtc_service', state: 'pending' },
  ],
};

export const mockStreamStatus: StreamStatus[] = [
  {
    stream: 'main',
    codec: 'H.264',
    resolution: mockVideoConfig.streams.main.resolution,
    fps: 25,
    bitrateKbps: 12288,
    state: 'running',
    browserCodec: true,
    hlsReady: true,
    flvReady: true,
    webrtcReady: true,
  },
  {
    stream: 'sub',
    codec: 'H.264',
    resolution: mockVideoConfig.streams.sub.resolution,
    fps: 15,
    bitrateKbps: 768,
    state: 'running',
    browserCodec: true,
    hlsReady: true,
    flvReady: true,
    webrtcReady: true,
  },
];

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

export function cloneDefaultConfig<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}
