import type { StreamName } from '../core';

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
    net_queue_checked_connections?: number;
    net_queue_critical?: number;
    net_queue_critical_connections?: number;
    net_queue_level?: string;
    net_queue_level_value?: number;
    net_queue_recovering?: number;
    net_queue_tracked?: number;
    net_queue_warning?: number;
    net_slow_client_history?: number;
    net_slow_clients?: number;
    net_stat_checks?: number;
    net_stat_enabled?: boolean;
    rtsp_active_sessions?: number;
    webrtc_active_peers?: number;
    webrtc_dtls_ready?: boolean;
    webrtc_enabled?: boolean;
    webrtc_ice_ready?: boolean;
    webrtc_ice_servers?: number;
    webrtc_local_port_base?: number;
    webrtc_max_peers?: number;
    webrtc_open_peers?: number;
    webrtc_public_ip?: string;
    webrtc_selected_ice_pairs?: number;
    webrtc_signaling_ready?: boolean;
    webrtc_srtp_ready?: boolean;
}
