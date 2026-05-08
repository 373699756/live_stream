import type {
  ImageConfig,
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

export const mockVideoConfig: VideoConfig = {
  streams: {
    main: {
      enabled: true,
      name: 'main',
      codec: 'h265',
      profile: 'main',
      h265_profile: 'main',
      resolution: '1920x1080',
      fps: 25,
      bitrate_kbps: 4096,
      rate_control: 'cbr',
      gop: 50,
      vbr_quality: 60,
      smart_codec: true,
    },
    sub: {
      enabled: true,
      name: 'sub',
      codec: 'h264',
      profile: 'main',
      h265_profile: 'main',
      resolution: '640x360',
      fps: 15,
      bitrate_kbps: 768,
      rate_control: 'cbr',
      gop: 30,
      vbr_quality: 55,
      smart_codec: false,
    },
  },
  source: { sensor: 'default' },
};

export const mockMediaCapabilities: MediaCapabilities = {
  streams: {
    main: {
      stream: 'main',
      available: true,
      codecs: [
        { codec: 'h264', profiles: ['baseline', 'main', 'high'] },
        { codec: 'h265', profiles: ['main'] },
        { codec: 'jpeg', profiles: ['baseline'] },
        { codec: 'mjpeg', profiles: ['baseline'] },
      ],
      resolutions: [
        { width: 3840, height: 2160 },
        { width: 2560, height: 1440 },
        { width: 1920, height: 1080 },
        { width: 1280, height: 720 },
      ],
      fps: { min: 1, max: 30 },
      bitrate_kbps: { min: 512, max: 8192 },
      rate_control: ['cbr', 'vbr', 'fixqp'],
      gop: { min: 1, max: 120 },
      smart_codec: true,
    },
    sub: {
      stream: 'sub',
      available: true,
      codecs: [
        { codec: 'h264', profiles: ['baseline', 'main', 'high'] },
        { codec: 'h265', profiles: ['main'] },
        { codec: 'jpeg', profiles: ['baseline'] },
        { codec: 'mjpeg', profiles: ['baseline'] },
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
      smart_codec: true,
    },
  },
  image: {
    basic: {
      brightness: { min: 0, max: 100, default: 50 },
      contrast: { min: 0, max: 100, default: 50 },
      saturation: { min: 0, max: 100, default: 50 },
      sharpness: { min: 0, max: 100, default: 50 },
      hue: { min: 0, max: 100, default: 50 },
    },
    exposure: {
      options: {
        mode: { values: ['auto', 'manual'], default: 'auto' },
        anti_flicker: { values: ['50hz', '60hz', 'off'], default: '50hz' },
        exposure_time: { values: ['auto', '1/25', '1/50', '1/100', '1/250'], default: 'auto' },
        gain: { values: ['auto', 'low', 'medium', 'high'], default: 'auto' },
        slow_shutter: { values: ['false', 'true'], default: 'true' },
        max_exposure_time: { values: ['1/12', '1/25', '1/50'], default: '1/25' },
      },
      ranges: { compensation: { min: 0, max: 100, default: 50 } },
    },
    white_balance: {
      options: {
        mode: { values: ['auto', 'manual', 'indoor', 'outdoor'], default: 'auto' },
      },
      ranges: {
        red_gain: { min: 0, max: 100, default: 50 },
        blue_gain: { min: 0, max: 100, default: 50 },
      },
    },
    enhancement: {
      options: { defog: { values: ['false', 'true'], default: 'false' } },
      ranges: {
        denoise_2d: { min: 0, max: 100, default: 50 },
        denoise_3d: { min: 0, max: 100, default: 50 },
        gamma: { min: 0, max: 100, default: 50 },
      },
    },
    backlight: {
      options: {
        mode: { values: ['off', 'wdr', 'blc', 'hlc'], default: 'off' },
      },
      ranges: { level: { min: 0, max: 100, default: 50 } },
    },
    color_mode: {
      mode: { values: ['color', 'black_white', 'auto'], default: 'color' },
    },
    orientation: { mirror: true, flip: true },
  },
};

export const mockImageConfig: ImageConfig = {
  basic: { brightness: 50, contrast: 50, saturation: 50, sharpness: 50, hue: 50 },
  exposure: {
    mode: 'auto',
    anti_flicker: '50hz',
    exposure_time: 'auto',
    gain: 'auto',
    compensation: 50,
    slow_shutter: true,
    max_exposure_time: '1/25',
  },
  white_balance: { mode: 'auto', red_gain: 50, blue_gain: 50 },
  enhancement: { denoise_2d: 50, denoise_3d: 50, defog: false, gamma: 50 },
  backlight: { mode: 'off', level: 50 },
  orientation: { mirror: false, flip: false },
  color_mode: { mode: 'color' },
};

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

export const mockSnapshotConfig: SnapshotConfig = {
  enabled: true,
  main_path: '/api/snapshot/main.jpg',
  sub_path: '/api/snapshot/sub.jpg',
  jpeg_quality: 85,
  timeout_ms: 2000,
};

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
  enabled: false,
  signaling_path: '/api/webrtc',
  ice_servers: [],
  max_peers: 4,
  prefer_tcp: false,
};

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
    codec: 'H.265',
    resolution: mockVideoConfig.streams.main.resolution,
    fps: 25,
    bitrateKbps: 4096,
    state: 'running',
  },
  {
    stream: 'sub',
    codec: 'H.264',
    resolution: mockVideoConfig.streams.sub.resolution,
    fps: 15,
    bitrateKbps: 768,
    state: 'running',
  },
];

export function cloneDefaultConfig<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}
