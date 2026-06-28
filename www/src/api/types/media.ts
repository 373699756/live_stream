import type {
    GopMode,
    ImageStrategyMode,
    NumberRange,
    Resolution,
    StreamName,
    VideoCodecName,
} from './core';

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

export interface MediaStreamInfo {
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
    active_subscriptions: number;
    preview_clients: number;
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
    items: MediaStreamInfo[];
}

export interface MediaPreviewUrls {
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
    subscription_id?: number;
    subscription_open?: boolean;
    subscription_generation?: number;
    subscription_pending_frames?: number;
    subscription_waiting_keyframe?: boolean;
    subscription_slow?: boolean;
    subscription_close_reason?: string;
    ice_selected?: boolean;
    dtls_state?: string;
    srtp_ready?: boolean;
    rtp_packets?: number;
    rtp_bytes?: number;
    rtcp_packets?: number;
    rtcp_bytes?: number;
    rtcp_pli_packets?: number;
    rtcp_fir_packets?: number;
    rtcp_nack_packets?: number;
    rtcp_transport_cc_packets?: number;
    rtcp_keyframe_requests?: number;
    last_rtcp_ms?: number;
    last_error?: string;
    pending_bytes?: number;
    send_queue_length?: number;
    last_write_at_ms?: number;
    close_reason?: string;
    media_running?: boolean;
    media_track_ready?: boolean;
    media_codec?: string;
    media_codec_generation?: number;
    media_http_flv_ready?: boolean;
    media_mjpeg_ready?: boolean;
    media_last_dts?: number;
    media_last_reset_reason?: string;
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
    webrtc_ice_servers?: number;
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
    subscription_id: number;
    subscription_open: boolean;
    subscription_generation: number;
    subscription_pending_frames: number;
    subscription_waiting_keyframe: boolean;
    subscription_slow: boolean;
    subscription_close_reason: string;
    ice_selected: boolean;
    dtls_state: string;
    srtp_ready: boolean;
    rtp_packets: number;
    rtp_bytes: number;
    rtcp_packets: number;
    rtcp_bytes: number;
    rtcp_pli_packets: number;
    rtcp_fir_packets: number;
    rtcp_nack_packets: number;
    rtcp_transport_cc_packets: number;
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
