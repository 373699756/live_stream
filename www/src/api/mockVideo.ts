import type {
  MediaCapabilities,
  StreamStatus,
  VideoConfig,
} from './types';

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
      gop_mode: 'normal_p',
      smart_codec: false,
    },
    sub: {
      enabled: true,
      codec: 'h264',
      resolution: '640x360',
      fps: 15,
      bitrate_kbps: 768,
      rate_control: 'cbr',
      gop: 30,
      gop_mode: 'normal_p',
      smart_codec: false,
    },
  },
};

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
      smart_codec: true,
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
      smart_codec: true,
    },
  },
  image: {
    basic: {
      brightness: { min: 0, max: 100, default: 50 },
      contrast: { min: 0, max: 100, default: 50 },
      saturation: { min: 0, max: 100, default: 52 },
      sharpness: { min: 0, max: 100, default: 42 },
      hue: { min: 0, max: 100, default: 50 },
    },
    exposure: {
      options: {
        mode: { values: ['auto', 'manual'], default: 'auto' },
        anti_flicker: { values: ['50hz', '60hz', 'off'], default: '50hz' },
        exposure_time: {
          values: ['auto', '1/12', '1/25', '1/50', '1/100', '1/250'],
          default: 'auto',
        },
        max_exposure_time: {
          values: ['1/12', '1/25', '1/50', '1/100', '1/250'],
          default: '1/25',
        },
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
        denoise_2d: { min: 0, max: 100, default: 60 },
        denoise_3d: { min: 0, max: 100, default: 52 },
        gamma: { min: 0, max: 100, default: 50 },
      },
    },
    backlight: {
      options: {
        mode: { values: ['off', 'drc'], default: 'off' },
      },
      ranges: { level: { min: 0, max: 100, default: 50 } },
    },
    color_mode: {
      mode: { values: ['color', 'black_white'], default: 'color' },
    },
    orientation: { mirror: true, flip: true },
  },
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
    hlsSupported: true,
    flvSupported: true,
    mjpegSupported: false,
    hlsReady: false,
    flvReady: false,
    mjpegReady: false,
    webrtcReady: false,
  },
  {
    stream: 'sub',
    codec: 'H.265',
    resolution: mockVideoConfig.streams.sub.resolution,
    fps: 15,
    bitrateKbps: 768,
    state: 'running',
    browserCodec: true,
    hlsSupported: true,
    flvSupported: true,
    mjpegSupported: false,
    hlsReady: true,
    flvReady: true,
    mjpegReady: false,
    webrtcReady: false,
  },
];
