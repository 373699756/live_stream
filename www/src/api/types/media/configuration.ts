import type {
    GopMode,
    ImageStrategyMode,
    NumberRange,
    Resolution,
    StreamName,
    VideoCodecName,
} from '../core';

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
    live_update_supported?: boolean;
}

export interface OptionControlCapability {
    values: string[];
    default: string;
    live_update_supported?: boolean;
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
        buffer_frames: number;
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

export interface ImageInfo {
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
