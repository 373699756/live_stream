// Image / ISP API: /api/config/image

import { mockImageConfig, mockImageInfo } from './mockImage';
import { requestJson, putJson, type ApiRequestOptions } from './client';
import type { ImageConfig, ImageInfo } from './types';

function normalizeImageConfig(config: ImageConfig): ImageConfig {
    const next: ImageConfig = {
        ...config,
        exposure: { ...config.exposure },
        backlight: { ...config.backlight },
        color_mode: { ...config.color_mode },
        lens_correction: {
            enabled: config.lens_correction?.enabled ?? false,
            aspect: config.lens_correction?.aspect ?? true,
            x_ratio: config.lens_correction?.x_ratio ?? 100,
            y_ratio: config.lens_correction?.y_ratio ?? 100,
            xy_ratio: config.lens_correction?.xy_ratio ?? 100,
            center_x_offset: config.lens_correction?.center_x_offset ?? 0,
            center_y_offset: config.lens_correction?.center_y_offset ?? 0,
            distortion_ratio: config.lens_correction?.distortion_ratio ?? 0,
        },
        stabilization: {
            enabled: config.stabilization?.enabled ?? false,
            motion_level: config.stabilization?.motion_level ?? 'normal',
            crop_ratio: config.stabilization?.crop_ratio ?? 80,
            buffer_frames: config.stabilization?.buffer_frames ?? 6,
            frame_rate: config.stabilization?.frame_rate ?? 30,
            moving_subject_level:
                config.stabilization?.moving_subject_level ?? 0,
            rolling_shutter_coef:
                config.stabilization?.rolling_shutter_coef ?? 0,
            horizontal_limit: config.stabilization?.horizontal_limit ?? 512,
            vertical_limit: config.stabilization?.vertical_limit ?? 512,
        },
        strategy: config.strategy
            ? { ...config.strategy }
            : { enabled: true, mode: 'low_noise' },
    };
    if (!next.color_mode.mode) {
        next.color_mode.mode = 'color';
    }
    if (!next.strategy?.mode) {
        next.strategy = {
            enabled: next.strategy?.enabled ?? true,
            mode: 'low_noise',
        };
    }
    return next;
}

export function getImageConfig(
    options?: ApiRequestOptions,
): Promise<ImageConfig> {
    return requestJson<ImageConfig>(
        '/api/config/image',
        mockImageConfig,
        options,
    ).then(normalizeImageConfig);
}

export function saveImageConfig(
    value: ImageConfig,
    options?: ApiRequestOptions,
): Promise<void> {
    return putJson('/api/config/image', value, options);
}

export function getImageInfo(options?: ApiRequestOptions): Promise<ImageInfo> {
    return requestJson<ImageInfo>(
        '/api/status/image-strategy',
        mockImageInfo,
        options,
    );
}
