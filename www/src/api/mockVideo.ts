import type { MediaCapabilities, VideoConfig } from './types/media/configuration';

export const mockVideoConfig: VideoConfig = {
    streams: {
        main: {
            enabled: true,
            codec: 'h264',
            resolution: '1920x1080',
            fps: 30,
            bitrate_kbps: 12288,
            rate_control: 'cbr',
            gop: 30,
            gop_mode: 'normal_p',
            smart_codec: false,
            roi: { enabled: false, regions: [] },
        },
        sub: {
            enabled: true,
            codec: 'h264',
            resolution: '1280x720',
            fps: 30,
            bitrate_kbps: 3072,
            rate_control: 'cbr',
            gop: 30,
            gop_mode: 'normal_p',
            smart_codec: false,
            roi: { enabled: false, regions: [] },
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
                { codec: 'mjpeg', profiles: [] },
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
            roi_supported: true,
            max_roi_regions: 8,
        },
        sub: {
            stream: 'sub',
            available: true,
            codecs: [
                { codec: 'h264', profiles: ['baseline', 'main', 'high'] },
                { codec: 'h265', profiles: ['main'] },
                { codec: 'mjpeg', profiles: [] },
            ],
            resolutions: [
                { width: 1280, height: 720 },
                { width: 704, height: 576 },
                { width: 640, height: 360 },
                { width: 352, height: 288 },
            ],
            fps: { min: 1, max: 30 },
            bitrate_kbps: { min: 64, max: 4096 },
            rate_control: ['cbr', 'vbr', 'fixqp'],
            gop: { min: 1, max: 120 },
            smart_codec: true,
            roi_supported: true,
            max_roi_regions: 8,
        },
    },
    image: {
        basic: {
            brightness: { min: 0, max: 100, default: 50 },
            contrast: { min: 0, max: 100, default: 50 },
            saturation: { min: 0, max: 100, default: 52 },
            sharpness: { min: 0, max: 100, default: 32 },
            hue: { min: 0, max: 100, default: 50 },
        },
        exposure: {
            options: {
                mode: { values: ['auto', 'manual'], default: 'auto' },
                anti_flicker: {
                    values: ['50hz', '60hz', 'off'],
                    default: '50hz',
                },
                exposure_time: {
                    values: [
                        'auto',
                        '1/12',
                        '1/25',
                        '1/30',
                        '1/50',
                        '1/100',
                        '1/250',
                    ],
                    default: 'auto',
                },
                max_exposure_time: {
                    values: ['1/12', '1/25', '1/30', '1/50', '1/100', '1/250'],
                    default: '1/30',
                },
                gain: {
                    values: ['auto', 'low', 'medium', 'high'],
                    default: 'auto',
                },
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
        lens_correction: {
            supported: true,
            min_width: 640,
            min_height: 480,
            options: {
                aspect: { values: ['false', 'true'], default: 'true' },
            },
            ranges: {
                x_ratio: { min: 0, max: 100, default: 100 },
                y_ratio: { min: 0, max: 100, default: 100 },
                xy_ratio: { min: 0, max: 100, default: 100 },
                center_x_offset: { min: -511, max: 511, default: 0 },
                center_y_offset: { min: -511, max: 511, default: 0 },
                distortion_ratio: { min: -300, max: 500, default: 0 },
            },
        },
        stabilization: {
            supported: true,
            min_width: 1280,
            min_height: 720,
            options: {
                motion_level: {
                    values: ['low', 'normal', 'high'],
                    default: 'normal',
                },
            },
            ranges: {
                crop_ratio: { min: 50, max: 98, default: 80 },
                buffer_frames: { min: 5, max: 10, default: 6 },
                frame_rate: { min: 1, max: 60, default: 30 },
                moving_subject_level: { min: 0, max: 6, default: 0 },
                rolling_shutter_coef: { min: 0, max: 1000, default: 0 },
                horizontal_limit: { min: 0, max: 1000, default: 512 },
                vertical_limit: { min: 0, max: 1000, default: 512 },
            },
        },
        orientation: { mirror: true, flip: true },
    },
};
