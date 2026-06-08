import type { ImageConfig, ImageStrategyStatus } from './types';

export const mockImageConfig: ImageConfig = {
  basic: { brightness: 50, contrast: 50, saturation: 52, sharpness: 32, hue: 50 },
  exposure: {
    mode: 'auto',
    anti_flicker: '50hz',
    exposure_time: 'auto',
    gain: 'auto',
    compensation: 50,
    slow_shutter: false,
    max_exposure_time: '1/30',
  },
  white_balance: { mode: 'auto', red_gain: 50, blue_gain: 45 },
  enhancement: { denoise_2d: 68, denoise_3d: 62, defog: false, gamma: 50 },
  backlight: { mode: 'off', level: 50 },
  orientation: { mirror: false, flip: false },
  color_mode: { mode: 'color' },
  strategy: { enabled: true, mode: 'low_noise' },
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
  mode: 'low_noise',
  tier: 'day',
  saturation: 52,
  sharpness: 26,
  denoise_2d: 76,
  denoise_3d: 72,
  gamma: 50,
};
